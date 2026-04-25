#include "runtime/ScreenClient.h"

#include <string.h>

namespace screenlib::client {

ScreenClient::ScreenClient(ITransport& transport)
    : _transport(transport), _bridge(transport) {}

Envelope& ScreenClient::prepareEnvelope(pb_size_t payloadTag) {
    _scratchEnvelope = Envelope_init_zero;
    _scratchEnvelope.which_payload = payloadTag;
    return _scratchEnvelope;
}

void ScreenClient::setUiAdapter(screenlib::adapter::IUiAdapter* uiAdapter) {
    // Отвязываем sink от старого адаптера, чтобы не оставить висячий обработчик.
    if (_uiAdapter != nullptr && _initialized) {
        _uiAdapter->setEventSink(nullptr, nullptr);
    }

    _uiAdapter = uiAdapter;
    _dispatcher.reset();

    if (_uiAdapter != nullptr) {
        _dispatcher.reset(new CommandDispatcher(*_uiAdapter));
        if (_initialized) {
            _uiAdapter->setEventSink(&ScreenClient::onUiEventStatic, this);
        }
    }
}

void ScreenClient::init() {
    if (_initialized) {
        return;
    }

    _bridge.setEnvelopeHandler(&ScreenClient::onBridgeEnvelopeStatic, this);

    if (_uiAdapter != nullptr) {
        _uiAdapter->setEventSink(&ScreenClient::onUiEventStatic, this);
    }

    _initialized = true;
}

void ScreenClient::tick() {
    if (!_initialized) {
        return;
    }

    // 1) Принять и декодировать входящие сообщения из транспорта.
    _bridge.processIncoming();

    // 2) Дать UI адаптеру сгенерировать пользовательские события.
    if (_uiAdapter != nullptr) {
        _uiAdapter->tickInput();
    }

    // 3) Отправить накопленные UI-события назад в контроллер.
    flushUiEvents();
}

bool ScreenClient::connected() const {
    return _bridge.connected();
}

void ScreenClient::setEventHandler(EventHandler handler, void* userData) {
    _eventHandler = handler;
    _eventUser = userData;
}

bool ScreenClient::sendHeartbeat(uint32_t uptimeMs) {
    Envelope& env = prepareEnvelope(Envelope_heartbeat_tag);
    env.payload.heartbeat.uptime_ms = uptimeMs;
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendButtonEvent(uint32_t elementId, uint32_t pageId, ButtonAction action) {
    Envelope& env = prepareEnvelope(Envelope_button_event_tag);
    env.payload.button_event.element_id = elementId;
    env.payload.button_event.page_id = pageId;
    env.payload.button_event.action = action;
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendInputEventInt(uint32_t elementId, uint32_t pageId, int32_t value) {
    Envelope& env = prepareEnvelope(Envelope_input_event_tag);
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_int_value_tag;
    env.payload.input_event.value.int_value = value;
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendInputEventString(uint32_t elementId, uint32_t pageId, const char* text) {
    Envelope& env = prepareEnvelope(Envelope_input_event_tag);
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_string_value_tag;
    copyTextSafe(
        env.payload.input_event.value.string_value,
        sizeof(env.payload.input_event.value.string_value),
        text
    );
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendEnvelope(const Envelope& env) {
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendHello(const DeviceInfo& deviceInfo) {
    return _bridge.sendHello(deviceInfo);
}

bool ScreenClient::sendCurrentPage(uint32_t pageId, uint32_t requestId) {
    return _bridge.sendCurrentPage(pageId, requestId);
}

bool ScreenClient::sendPageState(const PageState& pageState) {
    return _bridge.sendPageState(pageState);
}

bool ScreenClient::sendElementState(const ElementState& elementState) {
    return _bridge.sendElementState(elementState);
}

// Ответ на request_element_attribute (screen -> host).
bool ScreenClient::sendElementAttributeState(const ElementAttributeState& state) {
    return _bridge.sendElementAttributeState(state);
}

void ScreenClient::onBridgeEnvelopeStatic(const Envelope& env, void* userData) {
    ScreenClient* self = static_cast<ScreenClient*>(userData);
    if (self != nullptr) {
        self->onBridgeEnvelope(env);
    }
}

void ScreenClient::onBridgeEnvelope(const Envelope& env) {
    if (_eventHandler != nullptr) {
        _eventHandler(env, EventDirection::Incoming, _eventUser);
    }

    if (_dispatcher != nullptr) {
        _dispatcher->dispatch(env);
    }
}

bool ScreenClient::onUiEventStatic(const Envelope& env, void* userData) {
    ScreenClient* self = static_cast<ScreenClient*>(userData);
    if (self == nullptr) {
        return false;
    }
    return self->onUiEvent(env);
}

bool ScreenClient::onUiEvent(const Envelope& env) {
    // Наружу пропускаем только пользовательские события экрана.
    // Любые другие payload от UI-адаптера игнорируем.
    if (env.which_payload != Envelope_button_event_tag &&
        env.which_payload != Envelope_input_event_tag) {
        return false;
    }

    return enqueueUiEvent(env);
}

bool ScreenClient::enqueueUiEvent(const Envelope& env) {
    if (_uiEventCount >= kUiEventQueueSize) {
        return false;
    }

    _uiEvents[_uiEventHead] = env;
    _uiEventHead = (_uiEventHead + 1) % kUiEventQueueSize;
    _uiEventCount++;
    return true;
}

bool ScreenClient::flushUiEvents() {
    bool allSent = true;

    while (_uiEventCount > 0) {
        Envelope& env = _uiEvents[_uiEventTail];
        if (!sendOutgoingEnvelope(env)) {
            allSent = false;
            break;
        }

        _uiEventTail = (_uiEventTail + 1) % kUiEventQueueSize;
        _uiEventCount--;
    }

    return allSent;
}

bool ScreenClient::sendOutgoingEnvelope(const Envelope& env) {
    const bool ok = _bridge.sendEnvelope(env);
    if (ok && _eventHandler != nullptr) {
        _eventHandler(env, EventDirection::Outgoing, _eventUser);
    }
    return ok;
}

void ScreenClient::copyTextSafe(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

}  // namespace screenlib::client
