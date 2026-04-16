#pragma once

#include <stddef.h>
#include <stdint.h>

#include "types/ScreenTypes.h"
#include "bridge/ScreenBridge.h"

namespace screenlib {

// Endpoint одного экрана (одного канала ScreenBridge).
class ScreenEndpoint {
public:
    // Callback входящих сообщений с контекстом источника.
    using IncomingHandler = void (*)(const Envelope& env, const ScreenEventContext& ctx, void* userData);

    ScreenEndpoint() = default;

    // Привязать bridge и параметры endpoint.
    void configure(uint8_t endpointId, EndpointRole role, ScreenBridge* bridge, bool enabled);

    // Обновить флаг активности endpoint без перепривязки bridge.
    void setEnabled(bool enabled) { _enabled = enabled; }

    // Установить обработчик входящих сообщений.
    void setIncomingHandler(IncomingHandler handler, void* userData = nullptr) {
        _incomingHandler = handler;
        _incomingUser = userData;
    }

    // Прокачка входящих данных endpoint.
    size_t tick();

    bool connected() const;
    bool enabled() const { return _enabled; }
    bool isBound() const { return _bridge != nullptr; }

    uint8_t id() const { return _id; }
    EndpointRole role() const { return _role; }

    // Тонкие proxy-методы отправки.
    bool showPage(uint32_t pageId);
    bool setText(uint32_t elementId, const char* text);
    bool setValue(uint32_t elementId, int32_t value);
    bool setVisible(uint32_t elementId, bool visible);
    bool sendHeartbeat(uint32_t uptimeMs);
    bool sendBatch(const SetBatch& batch);

    // Service request helper-методы (host -> screen).
    bool requestDeviceInfo(uint32_t requestId = 0);
    bool requestCurrentPage(uint32_t requestId = 0);
    bool requestPageState(uint32_t pageId, uint32_t requestId = 0);

private:
    uint8_t _id = 0;
    EndpointRole _role = EndpointRole::Aux;
    bool _enabled = false;
    ScreenBridge* _bridge = nullptr;

    IncomingHandler _incomingHandler = nullptr;
    void* _incomingUser = nullptr;

    bool canUseBridge() const;
    ScreenEventContext makeEventContext() const;

    // Callback от ScreenBridge.
    static void onBridgeEnvelope(const Envelope& env, void* userData);
};

}  // namespace screenlib
