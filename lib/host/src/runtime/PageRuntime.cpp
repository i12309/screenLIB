#include "PageRuntime.h"

#include <cstring>

#include "bridge/ScreenBridge.h"
#include "config/ScreenConfig.h"
#include "log/ScreenLibLogger.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#endif

namespace screenlib {

namespace {

constexpr const char* kLogTag = "screenlib.runtime";

// Монотонное время в мс. Используется если пользователь не инжектил свой provider.
uint32_t default_monotonic_ms() {
#if defined(ARDUINO)
    return millis();
#else
    using clock = std::chrono::steady_clock;
    static const auto kStart = clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - kStart);
    return static_cast<uint32_t>(elapsed.count());
#endif
}

// Перенос значения из ElementAttributeValue в oneof SetElementAttribute.
// Возвращает false если тип в ElementAttributeValue не поддерживается SetElementAttribute.
bool copyAttributeValueToSetCmd(const ElementAttributeValue& src, SetElementAttribute& dst) {
    dst.attribute = src.attribute;
    switch (src.which_value) {
        case ElementAttributeValue_int_value_tag:
            dst.which_value = SetElementAttribute_int_value_tag;
            dst.value.int_value = src.value.int_value;
            return true;
        case ElementAttributeValue_color_value_tag:
            dst.which_value = SetElementAttribute_color_value_tag;
            dst.value.color_value = src.value.color_value & 0x00FFFFFFu;
            return true;
        case ElementAttributeValue_font_value_tag:
            dst.which_value = SetElementAttribute_font_value_tag;
            dst.value.font_value = src.value.font_value;
            return true;
        case ElementAttributeValue_bool_value_tag:
            dst.which_value = SetElementAttribute_bool_value_tag;
            dst.value.bool_value = src.value.bool_value;
            return true;
        case ElementAttributeValue_string_value_tag:
            dst.which_value = SetElementAttribute_string_value_tag;
            std::strncpy(dst.value.string_value, src.value.string_value,
                         sizeof(dst.value.string_value) - 1);
            dst.value.string_value[sizeof(dst.value.string_value) - 1] = '\0';
            return true;
        default:
            return false;
    }
}

}  // namespace

// ---------- Lifecycle ----------

bool PageRuntime::init(const ScreenConfig& cfg) {
    _mirrorMode = cfg.mirrorMode;
    // В текущей версии init только сохраняет конфиг. Bridge подаются
    // извне через attachPhysicalBridge/attachWebBridge.
    return true;
}

void PageRuntime::tick() {
    if (_physical != nullptr) {
        _physical->poll();
    }
    if (_web != nullptr) {
        _web->poll();
    }

    if (_current != nullptr && _current->_shown) {
        _current->onTick();
    }

    checkPendingTimeouts();
}

void PageRuntime::attachPhysicalBridge(ScreenBridge* bridge) {
    _physical = bridge;
    if (_physical != nullptr) {
        _physical->setEnvelopeHandler(&PageRuntime::onBridgeEnvelope, this);
    }
}

void PageRuntime::attachWebBridge(ScreenBridge* bridge) {
    _web = bridge;
    if (_web != nullptr) {
        _web->setEnvelopeHandler(&PageRuntime::onBridgeEnvelope, this);
    }
}

bool PageRuntime::back() {
    if (_previousFactory == nullptr) {
        SCREENLIB_LOGW(kLogTag, "back: no previous page");
        return false;
    }
    auto prev = _previousFactory();
    if (prev == nullptr) {
        SCREENLIB_LOGW(kLogTag, "back: factory returned null");
        return false;
    }
    const uint32_t pid = prev->pageId();
    return swapCurrent(std::move(prev), _previousFactory, pid);
}

bool PageRuntime::swapCurrent(std::unique_ptr<IPage> next, PageFactory nextFactory, uint32_t pageId) {
    if (next == nullptr) return false;

    // 1. Дренаж: для простоты сейчас не ждём ACK'ов, а очищаем очередь.
    //    Бэк-оптимист: новая страница начнётся с PageSnapshot'а, который
    //    перезапишет состояние; висящие ACK устаревают по session_id.
    _pendingCount = 0;

    // 2. Закрыть старую страницу.
    if (_current != nullptr) {
        _current->onClose();
    }

    // 3. Установить новую страницу и обновить фабрики истории.
    _previousFactory = _currentFactory;
    _currentFactory = nextFactory;
    _current = std::move(next);
    _current->_runtime = this;
    _current->_shown = false;

    // 4. Новая эпоха и подготовка модели.
    ++_sessionCounter;
    _model.beginPage(pageId, _sessionCounter);

    // 5. Отправить ShowPage. onShow вызовется позже при приёме PageSnapshot.
    Envelope env{};
    env.which_payload = Envelope_show_page_tag;
    env.payload.show_page.page_id = pageId;
    env.payload.show_page.session_id = _sessionCounter;

    if (!sendEnvelopeByMode(env)) {
        SCREENLIB_LOGW(kLogTag, "swapCurrent: ShowPage send failed pageId=%u session=%u",
                       static_cast<unsigned>(pageId),
                       static_cast<unsigned>(_sessionCounter));
        setLinkUp(false);
        return false;
    }
    return true;
}

// ---------- State ----------

uint32_t PageRuntime::currentPageId() const {
    return _current != nullptr ? _current->pageId() : 0;
}

uint32_t PageRuntime::nowMs() const {
    return _now != nullptr ? _now() : default_monotonic_ms();
}

// ---------- sendSetAttribute ----------

RequestId PageRuntime::sendSetAttribute(uint32_t elementId, const ElementAttributeValue& v) {
    if (_pendingCount >= kMaxPending) {
        SCREENLIB_LOGW(kLogTag, "pending overflow (%u), linkDown",
                       static_cast<unsigned>(_pendingCount));
        setLinkUp(false);
        return kInvalidRequestId;
    }

    const RequestId reqId = _nextRequestId++;
    if (_nextRequestId == kInvalidRequestId) {
        _nextRequestId = 1;  // wrap, 0 зарезервирован
    }

    SetElementAttribute cmd = SetElementAttribute_init_zero;
    cmd.element_id = elementId;
    cmd.request_id = reqId;
    cmd.session_id = _model.sessionId();
    if (!copyAttributeValueToSetCmd(v, cmd)) {
        SCREENLIB_LOGW(kLogTag,
                       "sendSetAttribute: unsupported value tag=%u element=%u",
                       static_cast<unsigned>(v.which_value),
                       static_cast<unsigned>(elementId));
        return kInvalidRequestId;
    }

    Envelope env{};
    env.which_payload = Envelope_set_element_attribute_tag;
    env.payload.set_element_attribute = cmd;

    if (!sendEnvelopeByMode(env)) {
        setLinkUp(false);
        return kInvalidRequestId;
    }

    Pending& p = _pending[_pendingCount++];
    p.id = reqId;
    p.elementId = elementId;
    p.attribute = v.attribute;
    p.sentAtMs = nowMs();
    return reqId;
}

// ---------- Очередь и таймауты ----------

bool PageRuntime::removePending(RequestId id) {
    for (std::size_t i = 0; i < _pendingCount; ++i) {
        if (_pending[i].id == id) {
            // swap-and-pop: порядок очереди нам не важен для dispatch'а,
            // но важен для checkPendingTimeouts (ищет самую старую запись).
            // Делаем сдвиг чтобы сохранить FIFO-порядок по sentAtMs.
            for (std::size_t j = i; j + 1 < _pendingCount; ++j) {
                _pending[j] = _pending[j + 1];
            }
            --_pendingCount;
            return true;
        }
    }
    return false;
}

void PageRuntime::checkPendingTimeouts() {
    if (_pendingCount == 0) return;
    const uint32_t now = nowMs();
    // Ищем самую старую запись (минимальный sentAtMs).
    uint32_t minSent = _pending[0].sentAtMs;
    for (std::size_t i = 1; i < _pendingCount; ++i) {
        if (_pending[i].sentAtMs < minSent) {
            minSent = _pending[i].sentAtMs;
        }
    }
    if (now - minSent > kLinkTimeoutMs) {
        SCREENLIB_LOGW(kLogTag,
                       "pending timeout after %u ms, pending=%u, linkDown",
                       static_cast<unsigned>(now - minSent),
                       static_cast<unsigned>(_pendingCount));
        setLinkUp(false);
    }
}

void PageRuntime::setLinkUp(bool up) {
    if (_linkUp == up) return;
    _linkUp = up;
    SCREENLIB_LOGI(kLogTag, "link %s", up ? "up" : "down");
    if (_linkListener != nullptr) {
        _linkListener(up, _linkListenerUser);
    }
}

// ---------- Отправка по mirrorMode ----------

bool PageRuntime::sendEnvelopeByMode(const Envelope& env) {
    switch (_mirrorMode) {
        case MirrorMode::PhysicalOnly:
            return _physical != nullptr && _physical->sendEnvelope(env);
        case MirrorMode::WebOnly:
            return _web != nullptr && _web->sendEnvelope(env);
        case MirrorMode::Both: {
            bool any = false;
            if (_physical != nullptr) {
                any = _physical->sendEnvelope(env) || any;
            }
            if (_web != nullptr) {
                any = _web->sendEnvelope(env) || any;
            }
            return any;
        }
    }
    return false;
}

// ---------- Приём входящих ----------

void PageRuntime::onBridgeEnvelope(const Envelope& env, void* userData) {
    PageRuntime* self = static_cast<PageRuntime*>(userData);
    if (self != nullptr) {
        self->onEnvelope(env);
    }
}

void PageRuntime::onEnvelope(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_page_snapshot_tag: {
            const PageSnapshot& snap = env.payload.page_snapshot;
            // Отбрасываем snapshot от устаревшей эпохи/страницы.
            if (snap.session_id != _model.sessionId() || snap.page_id != _model.pageId()) {
                SCREENLIB_LOGW(kLogTag,
                               "stale snapshot ignored: page=%u/%u session=%u/%u",
                               static_cast<unsigned>(snap.page_id),
                               static_cast<unsigned>(_model.pageId()),
                               static_cast<unsigned>(snap.session_id),
                               static_cast<unsigned>(_model.sessionId()));
                return;
            }
            _model.applySnapshot(snap);
            _model.markReady();
            if (_current != nullptr && !_current->_shown) {
                _current->onShow();
                _current->_shown = true;
            }
            break;
        }
        case Envelope_attribute_changed_tag: {
            const AttributeChanged& msg = env.payload.attribute_changed;
            if (msg.in_reply_to_request != kInvalidRequestId) {
                removePending(msg.in_reply_to_request);
            }
            // applyRemoteChange сам проверит page_id/session_id и отбросит stale.
            _model.applyRemoteChange(msg);
            break;
        }
        case Envelope_button_event_tag: {
            const ButtonEvent& ev = env.payload.button_event;
            if (ev.session_id != 0 && ev.session_id != _model.sessionId()) {
                SCREENLIB_LOGW(kLogTag,
                               "stale button_event ignored: session=%u/%u element=%u",
                               static_cast<unsigned>(ev.session_id),
                               static_cast<unsigned>(_model.sessionId()),
                               static_cast<unsigned>(ev.element_id));
                return;
            }
            if (_current != nullptr) {
                _current->onButton(ev.element_id, ev.action);
            }
            break;
        }
        case Envelope_input_event_tag: {
            const InputEvent& ev = env.payload.input_event;
            if (ev.session_id != 0 && ev.session_id != _model.sessionId()) {
                SCREENLIB_LOGW(kLogTag,
                               "stale input_event ignored: session=%u/%u element=%u",
                               static_cast<unsigned>(ev.session_id),
                               static_cast<unsigned>(_model.sessionId()),
                               static_cast<unsigned>(ev.element_id));
                return;
            }
            if (_current == nullptr) return;
            switch (ev.which_value) {
                case InputEvent_int_value_tag:
                    _current->onInputInt(ev.element_id, ev.value.int_value);
                    break;
                case InputEvent_string_value_tag:
                    _current->onInputText(ev.element_id, ev.value.string_value);
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            // Остальные входящие (heartbeat, service responses) в текущем runtime
            // не обрабатываются — это задача верхнего слоя при необходимости.
            break;
    }
}

}  // namespace screenlib
