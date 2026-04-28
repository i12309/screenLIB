#include "PageRuntime.h"

#include <cstring>
#include <memory>

#include "bridge/ScreenBridge.h"
#include "chunk/TextChunkSender.h"
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
            return false;
        default:
            return false;
    }
}

bool sendEnvelopeToBridge(const Envelope& env, void* userData) {
    ScreenBridge* bridge = static_cast<ScreenBridge*>(userData);
    return bridge != nullptr && bridge->sendEnvelope(env);
}

const char* attributeName(ElementAttribute attribute) {
    switch (attribute) {
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH: return "POSITION_WIDTH";
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_HEIGHT: return "POSITION_HEIGHT";
        case ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR: return "BACKGROUND_COLOR";
        case ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_COLOR: return "BORDER_COLOR";
        case ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH: return "BORDER_WIDTH";
        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_COLOR: return "TEXT_COLOR";
        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT: return "TEXT_FONT";
        case ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE: return "VISIBLE";
        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT: return "TEXT";
        case ElementAttribute_ELEMENT_ATTRIBUTE_VALUE: return "VALUE";
        case ElementAttribute_ELEMENT_ATTRIBUTE_X: return "X";
        case ElementAttribute_ELEMENT_ATTRIBUTE_Y: return "Y";
        case ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN:
        default:
            return "UNKNOWN";
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

    TextChunkAbort abort = TextChunkAbort_init_zero;
    while (_textAssembler.pollTimeout(nowMs(), abort)) {
        SCREENLIB_LOGW(kLogTag,
                       "text chunk assembly timeout transfer=%u request=%u",
                       static_cast<unsigned>(abort.transfer_id),
                       static_cast<unsigned>(abort.request_id));
        sendTextChunkAbortByMode(abort);
    }

    if (_current != nullptr && _current->_shown) {
        _current->onTick();
    }

    flushQueuedSends();
    checkSnapshotTimeout();
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
    _queuedHead = 0;
    _queuedCount = 0;
    _queuedTextHead = 0;
    _queuedTextCount = 0;
    _textAssembler.reset();
    _snapshotRequestedAtMs = nowMs();
    _snapshotTimeoutLogged = false;
    _navState = RuntimeState::WaitingSnapshot;

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
    if (!sendShowPageByMode(pageId, _sessionCounter)) {
        SCREENLIB_LOGW(kLogTag, "swapCurrent: ShowPage send failed pageId=%u session=%u",
                       static_cast<unsigned>(pageId),
                       static_cast<unsigned>(_sessionCounter));
        setLinkUp(false);
        _navState = RuntimeState::LinkDown;
        return false;
    }
    setLinkUp(true);
    return true;
}

// ---------- State ----------

uint32_t PageRuntime::currentPageId() const {
    return _current != nullptr ? _current->pageId() : 0;
}

uint32_t PageRuntime::nowMs() const {
    return _now != nullptr ? _now() : default_monotonic_ms();
}

// ---------- IPage property model ----------

PageModel& IPage::writeTargetModel() {
    if (_runtime != nullptr && _runtime->model().isReady()) {
        return _runtime->model();
    }
    return _pendingModel;
}

const PageModel& IPage::readTargetModel(uint32_t elementId, ElementAttribute attribute) const {
    if (_pendingModel.has(elementId, attribute)) {
        return _pendingModel;
    }
    if (_runtime != nullptr && _runtime->model().isReady()) {
        return _runtime->model();
    }
    return _pendingModel;
}

int32_t IPage::readIntProperty(uint32_t elementId, ElementAttribute attribute) const {
    return readTargetModel(elementId, attribute).getInt(elementId, attribute);
}

bool IPage::readBoolProperty(uint32_t elementId, ElementAttribute attribute) const {
    return readTargetModel(elementId, attribute).getBool(elementId, attribute);
}

uint32_t IPage::readColorProperty(uint32_t elementId, ElementAttribute attribute) const {
    return readTargetModel(elementId, attribute).getColor(elementId, attribute);
}

ElementFont IPage::readFontProperty(uint32_t elementId, ElementAttribute attribute) const {
    return readTargetModel(elementId, attribute).getFont(elementId, attribute);
}

const char* IPage::readStringProperty(uint32_t elementId, ElementAttribute attribute) const {
    return readTargetModel(elementId, attribute).getString(elementId, attribute);
}

void IPage::writeAttributeValue(uint32_t elementId, const ElementAttributeValue& value) {
    PageModel& target = writeTargetModel();
    switch (value.which_value) {
        case ElementAttributeValue_int_value_tag:
            target.setInt(elementId, value.attribute, value.value.int_value);
            break;
        case ElementAttributeValue_bool_value_tag:
            target.setBool(elementId, value.attribute, value.value.bool_value);
            break;
        case ElementAttributeValue_color_value_tag:
            target.setColor(elementId, value.attribute, value.value.color_value);
            break;
        case ElementAttributeValue_font_value_tag:
            target.setFont(elementId, value.attribute, value.value.font_value);
            break;
        case ElementAttributeValue_string_value_tag:
            target.setString(elementId, value.attribute, value.value.string_value);
            break;
        default:
            return;
    }

    if (_runtime != nullptr && _runtime->model().isReady()) {
        _runtime->sendSetAttribute(elementId, value);
    }
}

void IPage::writeIntProperty(uint32_t elementId, ElementAttribute attribute, int32_t value) {
    ElementAttributeValue eav{};
    eav.attribute = attribute;
    eav.which_value = ElementAttributeValue_int_value_tag;
    eav.value.int_value = value;
    writeAttributeValue(elementId, eav);
}

void IPage::writeBoolProperty(uint32_t elementId, ElementAttribute attribute, bool value) {
    ElementAttributeValue eav{};
    eav.attribute = attribute;
    eav.which_value = ElementAttributeValue_bool_value_tag;
    eav.value.bool_value = value;
    writeAttributeValue(elementId, eav);
}

void IPage::writeColorProperty(uint32_t elementId, ElementAttribute attribute, uint32_t value) {
    ElementAttributeValue eav{};
    eav.attribute = attribute;
    eav.which_value = ElementAttributeValue_color_value_tag;
    eav.value.color_value = value & 0x00FFFFFFu;
    writeAttributeValue(elementId, eav);
}

void IPage::writeFontProperty(uint32_t elementId, ElementAttribute attribute, ElementFont value) {
    ElementAttributeValue eav{};
    eav.attribute = attribute;
    eav.which_value = ElementAttributeValue_font_value_tag;
    eav.value.font_value = value;
    writeAttributeValue(elementId, eav);
}

void IPage::writeStringProperty(uint32_t elementId, ElementAttribute attribute, const char* value) {
    const char* src = value != nullptr ? value : "";
    if (_runtime == nullptr || !_runtime->model().isReady()) {
        _pendingModel.setString(elementId, attribute, src);
        return;
    }

    _runtime->model().setString(elementId, attribute, src);
    _runtime->sendSetAttributeText(elementId, attribute, src);
}

namespace {

void applyPendingSlot(uint32_t elementId,
                      ElementAttribute attribute,
                      const AttributeValue& value,
                      void* user) {
    IPage* page = static_cast<IPage*>(user);
    if (page == nullptr) return;

    switch (value.type) {
        case AttributeValue::Type::Int:
            page->writeIntProperty(elementId, attribute, value.i);
            break;
        case AttributeValue::Type::Bool:
            page->writeBoolProperty(elementId, attribute, value.b);
            break;
        case AttributeValue::Type::Color:
            page->writeColorProperty(elementId, attribute, value.u);
            break;
        case AttributeValue::Type::Font:
            page->writeFontProperty(elementId, attribute, value.font);
            break;
        case AttributeValue::Type::String:
            page->writeStringProperty(elementId, attribute, value.s);
            break;
        case AttributeValue::Type::None:
        default:
            break;
    }
}

}  // namespace

void IPage::applyPendingAttributes() {
    if (_runtime == nullptr || !_runtime->model().isReady()) {
        return;
    }
    _pendingModel.forEachSlot(&applyPendingSlot, this);
    _pendingModel.clear();
}

// ---------- sendSetAttribute ----------

RequestId PageRuntime::sendSetAttributeText(uint32_t elementId,
                                            ElementAttribute attribute,
                                            const char* fullText) {
    if (_pendingCount >= kMaxPending) {
        SCREENLIB_LOGW(kLogTag, "pending overflow (%u), linkDown",
                       static_cast<unsigned>(_pendingCount));
        setLinkUp(false);
        return kInvalidRequestId;
    }

    // Для текста не используем ElementAttributeValue: его string_value ограничен 48 байтами.
    const RequestId reqId = _nextRequestId++;
    if (_nextRequestId == kInvalidRequestId) {
        _nextRequestId = 1;  // wrap, 0 зарезервирован
    }

    const char* src = fullText != nullptr ? fullText : "";
    if (_pendingCount >= kMaxInFlight) {
        if (!enqueueSetAttributeText(reqId, elementId, attribute, src)) {
            SCREENLIB_LOGW(kLogTag, "sendSetAttributeText: queue overflow (%u), linkDown",
                           static_cast<unsigned>(_queuedTextCount));
            setLinkUp(false);
            return kInvalidRequestId;
        }
        return reqId;
    }

    return sendSetAttributeTextNow(reqId, elementId, attribute, src) ? reqId : kInvalidRequestId;
}

bool PageRuntime::sendSetAttributeNow(RequestId reqId,
                                      uint32_t elementId,
                                      const ElementAttributeValue& v) {
    if (v.which_value == ElementAttributeValue_string_value_tag) {
        return sendSetAttributeTextNow(reqId, elementId, v.attribute, v.value.string_value);
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
        return false;
    }

    if (!sendSetElementAttributeByMode(cmd)) {
        setLinkUp(false);
        return false;
    }

    Pending& p = _pending[_pendingCount++];
    p.id = reqId;
    p.elementId = elementId;
    p.attribute = v.attribute;
    p.sentAtMs = nowMs();
    p.pageId = _model.pageId();
    p.sessionId = _model.sessionId();
    return true;
}

bool PageRuntime::sendSetAttributeTextNow(RequestId reqId,
                                          uint32_t elementId,
                                          ElementAttribute attribute,
                                          const char* text) {
    const uint32_t transferId = _nextTransferId++;
    if (_nextTransferId == 0) {
        _nextTransferId = 1;
    }

    const char* src = text != nullptr ? text : "";
    const std::size_t len = std::strlen(src);
    if (len > chunk::kMaxChunkedTextBytes) {
        SCREENLIB_LOGW(kLogTag,
                       "sendSetAttributeText: text too long element=%u attr=%s(%u) len=%u max=%u, truncated",
                       static_cast<unsigned>(elementId),
                       attributeName(attribute),
                       static_cast<unsigned>(attribute),
                       static_cast<unsigned>(len),
                       static_cast<unsigned>(chunk::kMaxChunkedTextBytes));
    }
    if (!sendTextChunksByMode(TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE,
                              transferId,
                              _model.sessionId(),
                              _model.pageId(),
                              elementId,
                              attribute,
                              reqId,
                              src)) {
        setLinkUp(false);
        return false;
    }

    Pending& p = _pending[_pendingCount++];
    p.id = reqId;
    p.elementId = elementId;
    p.attribute = attribute;
    p.sentAtMs = nowMs();
    p.pageId = _model.pageId();
    p.sessionId = _model.sessionId();
    return true;
}

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

    if (_pendingCount >= kMaxInFlight) {
        if (!enqueueSetAttribute(reqId, elementId, v)) {
            SCREENLIB_LOGW(kLogTag, "sendSetAttribute: queue overflow (%u), linkDown",
                           static_cast<unsigned>(_queuedCount));
            setLinkUp(false);
            return kInvalidRequestId;
        }
        return reqId;
    }

    return sendSetAttributeNow(reqId, elementId, v) ? reqId : kInvalidRequestId;
}

// ---------- Очередь и таймауты ----------

bool PageRuntime::enqueueSetAttribute(RequestId reqId, uint32_t elementId, const ElementAttributeValue& v) {
    if (_queuedCount >= kMaxQueued) {
        return false;
    }
    const std::size_t index = (_queuedHead + _queuedCount) % kMaxQueued;
    _queued[index].id = reqId;
    _queued[index].elementId = elementId;
    _queued[index].value = v;
    ++_queuedCount;
    return true;
}

bool PageRuntime::enqueueSetAttributeText(RequestId reqId,
                                          uint32_t elementId,
                                          ElementAttribute attribute,
                                          const char* text) {
    if (_queuedTextCount >= kMaxQueuedText) {
        return false;
    }

    const std::size_t index = (_queuedTextHead + _queuedTextCount) % kMaxQueuedText;
    _queuedText[index].id = reqId;
    _queuedText[index].elementId = elementId;
    _queuedText[index].attribute = attribute;

    // Очередь хранит полный текст до лимита чанков, а не 48-байтный proto-буфер.
    const char* src = text != nullptr ? text : "";
    const std::size_t len = std::strlen(src);
    if (len > chunk::kMaxChunkedTextBytes) {
        SCREENLIB_LOGW(kLogTag,
                       "enqueueSetAttributeText: text too long element=%u attr=%s(%u) len=%u max=%u, truncated",
                       static_cast<unsigned>(elementId),
                       attributeName(attribute),
                       static_cast<unsigned>(attribute),
                       static_cast<unsigned>(len),
                       static_cast<unsigned>(chunk::kMaxChunkedTextBytes));
    }
    std::strncpy(_queuedText[index].text, src, chunk::kMaxChunkedTextBytes);
    _queuedText[index].text[chunk::kMaxChunkedTextBytes] = '\0';
    ++_queuedTextCount;
    return true;
}

void PageRuntime::flushQueuedSends() {
    while (_pendingCount < kMaxInFlight && (_queuedCount > 0 || _queuedTextCount > 0)) {
        const bool takeValue =
            _queuedCount > 0 &&
            (_queuedTextCount == 0 || _queued[_queuedHead].id <= _queuedText[_queuedTextHead].id);

        if (takeValue) {
            Queued queued = _queued[_queuedHead];
            _queuedHead = (_queuedHead + 1) % kMaxQueued;
            --_queuedCount;
            sendSetAttributeNow(queued.id, queued.elementId, queued.value);
        } else {
            QueuedText queued = _queuedText[_queuedTextHead];
            _queuedTextHead = (_queuedTextHead + 1) % kMaxQueuedText;
            --_queuedTextCount;
            sendSetAttributeTextNow(queued.id, queued.elementId, queued.attribute, queued.text);
        }
    }
}

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

bool PageRuntime::removePendingForAck(RequestId id, uint32_t pageId, uint32_t sessionId) {
    for (std::size_t i = 0; i < _pendingCount; ++i) {
        if (_pending[i].id != id) {
            continue;
        }
        if (_pending[i].pageId != pageId || _pending[i].sessionId != sessionId) {
            SCREENLIB_LOGD(kLogTag,
                           "stale ACK ignored: request=%u page=%u/%u session=%u/%u",
                           static_cast<unsigned>(id),
                           static_cast<unsigned>(pageId),
                           static_cast<unsigned>(_pending[i].pageId),
                           static_cast<unsigned>(sessionId),
                           static_cast<unsigned>(_pending[i].sessionId));
            return false;
        }
        return removePending(id);
    }
    return false;
}

void PageRuntime::checkSnapshotTimeout() {
    if (_navState != RuntimeState::WaitingSnapshot || _snapshotTimeoutLogged) {
        return;
    }
    const uint32_t now = nowMs();
    if (now - _snapshotRequestedAtMs > kSnapshotTimeoutMs) {
        SCREENLIB_LOGW(kLogTag,
                       "snapshot timeout page=%u session=%u after %u ms",
                       static_cast<unsigned>(_model.pageId()),
                       static_cast<unsigned>(_model.sessionId()),
                       static_cast<unsigned>(now - _snapshotRequestedAtMs));
        _snapshotTimeoutLogged = true;
        setLinkUp(false);
        _navState = RuntimeState::LinkDown;
    }
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
        const std::size_t timedOutCount = _pendingCount;
        SCREENLIB_LOGW(kLogTag,
                       "pending timeout page=%u session=%u pending=%u after %u ms, linkDown",
                       static_cast<unsigned>(_pending[0].pageId),
                       static_cast<unsigned>(_pending[0].sessionId),
                       static_cast<unsigned>(timedOutCount),
                       static_cast<unsigned>(now - minSent));
        for (std::size_t i = 0; i < timedOutCount; ++i) {
            const Pending& p = _pending[i];
            SCREENLIB_LOGW(kLogTag,
                           "pending without ACK: request=%u page=%u session=%u element=%u attr=%s(%u) age=%u ms",
                           static_cast<unsigned>(p.id),
                           static_cast<unsigned>(p.pageId),
                           static_cast<unsigned>(p.sessionId),
                           static_cast<unsigned>(p.elementId),
                           attributeName(p.attribute),
                           static_cast<unsigned>(p.attribute),
                           static_cast<unsigned>(now - p.sentAtMs));
        }
        _pendingCount = 0;
        _queuedHead = 0;
        _queuedCount = 0;
        _queuedTextHead = 0;
        _queuedTextCount = 0;
        setLinkUp(false);
        _navState = RuntimeState::LinkDown;
    }
}

void PageRuntime::setLinkUp(bool up) {
    if (_linkUp == up) return;
    _linkUp = up;
    if (!up) {
        _navState = RuntimeState::LinkDown;
    }
    SCREENLIB_LOGI(kLogTag, "link %s", up ? "up" : "down");
    if (_linkListener != nullptr) {
        _linkListener(up, _linkListenerUser);
    }
}

// ---------- Отправка по mirrorMode ----------

bool PageRuntime::sendShowPageByMode(uint32_t pageId, uint32_t sessionId) {
    switch (_mirrorMode) {
        case MirrorMode::PhysicalOnly:
            return _physical != nullptr && _physical->showPage(pageId, sessionId);
        case MirrorMode::WebOnly:
            return _web != nullptr && _web->showPage(pageId, sessionId);
        case MirrorMode::Both: {
            bool any = false;
            if (_physical != nullptr) {
                any = _physical->showPage(pageId, sessionId) || any;
            }
            if (_web != nullptr) {
                any = _web->showPage(pageId, sessionId) || any;
            }
            return any;
        }
    }
    return false;
}

bool PageRuntime::sendSetElementAttributeByMode(const SetElementAttribute& cmd) {
    switch (_mirrorMode) {
        case MirrorMode::PhysicalOnly:
            return _physical != nullptr && _physical->setElementAttribute(cmd);
        case MirrorMode::WebOnly:
            return _web != nullptr && _web->setElementAttribute(cmd);
        case MirrorMode::Both: {
            bool any = false;
            if (_physical != nullptr) {
                any = _physical->setElementAttribute(cmd) || any;
            }
            if (_web != nullptr) {
                any = _web->setElementAttribute(cmd) || any;
            }
            return any;
        }
    }
    return false;
}

// ---------- Приём входящих ----------

bool PageRuntime::sendTextChunksByMode(TextChunkKind kind,
                                       uint32_t transferId,
                                       uint32_t sessionId,
                                       uint32_t pageId,
                                       uint32_t elementId,
                                       ElementAttribute attribute,
                                       uint32_t requestId,
                                       const char* text) {
    switch (_mirrorMode) {
        case MirrorMode::PhysicalOnly:
            return _physical != nullptr &&
                   chunk::sendTextChunks(&sendEnvelopeToBridge, _physical, kind, transferId,
                                         sessionId, pageId, elementId, attribute, requestId, text);
        case MirrorMode::WebOnly:
            return _web != nullptr &&
                   chunk::sendTextChunks(&sendEnvelopeToBridge, _web, kind, transferId,
                                         sessionId, pageId, elementId, attribute, requestId, text);
        case MirrorMode::Both: {
            bool any = false;
            if (_physical != nullptr) {
                any = chunk::sendTextChunks(&sendEnvelopeToBridge, _physical, kind, transferId,
                                            sessionId, pageId, elementId, attribute, requestId, text) || any;
            }
            if (_web != nullptr) {
                any = chunk::sendTextChunks(&sendEnvelopeToBridge, _web, kind, transferId,
                                            sessionId, pageId, elementId, attribute, requestId, text) || any;
            }
            return any;
        }
    }
    return false;
}

bool PageRuntime::sendTextChunkAbortByMode(const TextChunkAbort& abort) {
    std::unique_ptr<Envelope> env(new Envelope);
    std::memset(env.get(), 0, sizeof(*env));
    env->which_payload = Envelope_text_chunk_abort_tag;
    env->payload.text_chunk_abort = abort;

    switch (_mirrorMode) {
        case MirrorMode::PhysicalOnly:
            return _physical != nullptr && _physical->sendEnvelope(*env);
        case MirrorMode::WebOnly:
            return _web != nullptr && _web->sendEnvelope(*env);
        case MirrorMode::Both: {
            bool any = false;
            if (_physical != nullptr) {
                any = _physical->sendEnvelope(*env) || any;
            }
            if (_web != nullptr) {
                any = _web->sendEnvelope(*env) || any;
            }
            return any;
        }
    }
    return false;
}

void PageRuntime::notifyDeviceInfo(const DeviceInfo& info) {
    if (_deviceInfoListener != nullptr) {
        _deviceInfoListener(info, _deviceInfoListenerUser);
    }
}

void PageRuntime::onBridgeEnvelope(const Envelope& env, void* userData) {
    PageRuntime* self = static_cast<PageRuntime*>(userData);
    if (self != nullptr) {
        self->onEnvelope(env);
    }
}

void PageRuntime::onEnvelope(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_hello_tag:
            if (env.payload.hello.has_device_info) {
                notifyDeviceInfo(env.payload.hello.device_info);
            }
            break;

        case Envelope_device_info_tag:
            notifyDeviceInfo(env.payload.device_info);
            break;

        case Envelope_page_snapshot_tag: {
            const PageSnapshot& snap = env.payload.page_snapshot;
            // Отбрасываем snapshot от устаревшей эпохи/страницы.
            if (snap.session_id != _model.sessionId() || snap.page_id != _model.pageId()) {
                SCREENLIB_LOGD(kLogTag,
                               "stale snapshot ignored: page=%u/%u session=%u/%u",
                               static_cast<unsigned>(snap.page_id),
                               static_cast<unsigned>(_model.pageId()),
                               static_cast<unsigned>(snap.session_id),
                               static_cast<unsigned>(_model.sessionId()));
                return;
            }
            _model.applySnapshot(snap);
            _model.markReady();
            _navState = RuntimeState::PageReady;
            _snapshotTimeoutLogged = false;
            if (_current != nullptr && !_current->_shown) {
                _current->applyPendingAttributes();
                _current->onShow();
                _current->_shown = true;
            }
            break;
        }
        case Envelope_attribute_changed_tag: {
            const AttributeChanged& msg = env.payload.attribute_changed;
            SCREENLIB_LOGI(kLogTag,
                           "RX AttributeChanged page=%u session=%u element=%u reply=%u has_value=%d reason=%d",
                           static_cast<unsigned>(msg.page_id),
                           static_cast<unsigned>(msg.session_id),
                           static_cast<unsigned>(msg.element_id),
                           static_cast<unsigned>(msg.in_reply_to_request),
                           msg.has_value ? 1 : 0,
                           static_cast<int>(msg.reason));
            if (msg.in_reply_to_request != kInvalidRequestId) {
                removePendingForAck(msg.in_reply_to_request, msg.page_id, msg.session_id);
            }
            // applyRemoteChange сам проверит page_id/session_id и отбросит stale.
            _model.applyRemoteChange(msg);
            break;
        }
        case Envelope_text_chunk_tag: {
            chunk::AssembledText text;
            TextChunkAbort abort = TextChunkAbort_init_zero;
            if (!_textAssembler.push(env.payload.text_chunk, nowMs(), text, abort)) {
                if (abort.transfer_id != 0) {
                    SCREENLIB_LOGW(kLogTag,
                                   "text chunk rejected transfer=%u reason=%d",
                                   static_cast<unsigned>(abort.transfer_id),
                                   static_cast<int>(abort.reason));
                    sendTextChunkAbortByMode(abort);
                }
                break;
            }

            if (text.kind == TextChunkKind_TEXT_CHUNK_INPUT_EVENT) {
                if (text.sessionId != 0 && text.sessionId != _model.sessionId()) {
                    SCREENLIB_LOGD(kLogTag,
                                   "stale text input ignored: session=%u/%u element=%u",
                                   static_cast<unsigned>(text.sessionId),
                                   static_cast<unsigned>(_model.sessionId()),
                                   static_cast<unsigned>(text.elementId));
                    break;
                }
                if (_current != nullptr) {
                    _current->onInputText(text.elementId, text.text.c_str());
                }
                break;
            }

            if (text.kind == TextChunkKind_TEXT_CHUNK_ATTRIBUTE_CHANGED) {
                if (text.pageId != _model.pageId() || text.sessionId != _model.sessionId()) {
                    SCREENLIB_LOGD(kLogTag,
                                   "stale text attribute ignored: page=%u/%u session=%u/%u element=%u",
                                   static_cast<unsigned>(text.pageId),
                                   static_cast<unsigned>(_model.pageId()),
                                   static_cast<unsigned>(text.sessionId),
                                   static_cast<unsigned>(_model.sessionId()),
                                   static_cast<unsigned>(text.elementId));
                    break;
                }
                if (text.requestId != kInvalidRequestId) {
                    removePendingForAck(text.requestId, text.pageId, text.sessionId);
                }
                _model.setString(text.elementId, text.attribute, text.text.c_str());
            }
            break;
        }
        case Envelope_text_chunk_abort_tag: {
            const TextChunkAbort& abort = env.payload.text_chunk_abort;
            if (abort.request_id != kInvalidRequestId) {
                removePending(abort.request_id);
            }
            SCREENLIB_LOGW(kLogTag,
                           "peer aborted text chunk transfer=%u request=%u reason=%d",
                           static_cast<unsigned>(abort.transfer_id),
                           static_cast<unsigned>(abort.request_id),
                           static_cast<int>(abort.reason));
            break;
        }
        case Envelope_button_event_tag: {
            const ButtonEvent& ev = env.payload.button_event;
            if (ev.session_id != 0 && ev.session_id != _model.sessionId()) {
                SCREENLIB_LOGD(kLogTag,
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
                SCREENLIB_LOGD(kLogTag,
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
