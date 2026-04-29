#include "runtime/ScreenClient.h"

#include <string.h>

#include "chunk/TextChunkSender.h"

namespace screenlib::client {

namespace {

bool sendEnvelopeThroughClient(const Envelope& env, void* userData) {
    ScreenClient* client = static_cast<ScreenClient*>(userData);
    return client != nullptr && client->sendEnvelope(env);
}

}  // namespace

ScreenClient::ScreenClient(ITransport& transport)
    : _transport(transport), _bridge(transport) {}

Envelope& ScreenClient::prepareEnvelope(pb_size_t payloadTag) {
    memset(&_scratchEnvelope, 0, sizeof(_scratchEnvelope));
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
        _dispatcher.reset(new CommandDispatcher(*_uiAdapter,
                                                &ScreenClient::onDispatcherResponseStatic,
                                                this));
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
    tick(_autoTickNowMs++);
}

void ScreenClient::tick(uint32_t nowMs) {
    if (!_initialized) {
        return;
    }

    // 1) Принять и декодировать входящие сообщения из транспорта.
    _bridge.processIncoming();
    if (_dispatcher != nullptr) {
        _dispatcher->pollTimeout(nowMs);
    }

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
    const uint32_t transferId = _nextTransferId++;
    if (_nextTransferId == 0) {
        _nextTransferId = 1;
    }
    return chunk::sendTextChunks(&sendEnvelopeThroughClient,
                                 this,
                                 TextChunkKind_TEXT_CHUNK_INPUT_EVENT,
                                 transferId,
                                 0,
                                 pageId,
                                 elementId,
                                 ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN,
                                 0,
                                 text != nullptr ? text : "");
}

bool ScreenClient::sendEnvelope(const Envelope& env) {
    return sendOutgoingEnvelope(env);
}

bool ScreenClient::sendHello(const DeviceInfo& deviceInfo) {
    return _bridge.sendHello(deviceInfo);
}

bool ScreenClient::sendDeviceInfo(const DeviceInfo& deviceInfo) {
    return _bridge.sendDeviceInfo(deviceInfo);
}

bool ScreenClient::sendCurrentPage(uint32_t pageId, uint32_t requestId) {
    return _bridge.sendCurrentPage(pageId, requestId);
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
        env.which_payload != Envelope_input_event_tag &&
        env.which_payload != Envelope_text_chunk_tag) {
        return false;
    }

    return enqueueUiEvent(env);
}

bool ScreenClient::onDispatcherResponseStatic(const Envelope& env, void* userData) {
    ScreenClient* self = static_cast<ScreenClient*>(userData);
    if (self == nullptr) {
        return false;
    }
    return self->onDispatcherResponse(env);
}

bool ScreenClient::onDispatcherResponse(const Envelope& env) {
    return sendOutgoingEnvelope(env);
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
