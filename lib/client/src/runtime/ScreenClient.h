#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "IUiAdapter.h"
#include "bridge/ScreenBridge.h"
#include "link/ITransport.h"
#include "runtime/CommandDispatcher.h"

namespace screenlib::client {

// ScreenClient — runtime экранной стороны.
// Получает команды через ScreenBridge и применяет их к IUiAdapter.
// Также отправляет UI-события обратно в контроллер тем же протоколом Envelope.
class ScreenClient {
public:
    enum class EventDirection : uint8_t {
        Incoming = 0,
        Outgoing
    };

    using EventHandler = void (*)(const Envelope& env, EventDirection direction, void* userData);

    explicit ScreenClient(ITransport& transport);

    // Назначить UI-адаптер.
    // Можно вызывать до init() и после init() (перепривязка sink произойдет автоматически).
    void setUiAdapter(screenlib::adapter::IUiAdapter* uiAdapter);

    // Инициализировать runtime связи bridge <-> client <-> adapter.
    // Метод идемпотентный: повторный вызов безопасен и не меняет состояние.
    void init();

    // Главный цикл экранной стороны.
    void tick();

    bool connected() const;

    // Внешний обработчик для трассировки входящих/исходящих Envelope.
    void setEventHandler(EventHandler handler, void* userData = nullptr);

    bool sendHeartbeat(uint32_t uptimeMs);
    bool sendButtonEvent(uint32_t elementId,
                         uint32_t pageId,
                         ButtonAction action = ButtonAction_CLICK);
    bool sendInputEventInt(uint32_t elementId, uint32_t pageId, int32_t value);
    bool sendInputEventString(uint32_t elementId, uint32_t pageId, const char* text);
    bool sendEnvelope(const Envelope& env);

    // Сервисные вспомогательные методы ответов экранной стороны.
    bool sendHello(const DeviceInfo& deviceInfo);
    bool sendDeviceInfo(const DeviceInfo& deviceInfo);
    bool sendCurrentPage(uint32_t pageId, uint32_t requestId = 0);
    // Ответ на request_element_attribute.
    bool sendElementAttributeState(const ElementAttributeState& state);

private:
    // Размер внутренней очереди событий UI -> controller.
    static constexpr size_t kUiEventQueueSize = 8;

    ITransport& _transport;
    ScreenBridge _bridge;
    screenlib::adapter::IUiAdapter* _uiAdapter = nullptr;
    std::unique_ptr<CommandDispatcher> _dispatcher;

    bool _initialized = false;

    EventHandler _eventHandler = nullptr;
    void* _eventUser = nullptr;

    Envelope _uiEvents[kUiEventQueueSize] = {};
    Envelope _scratchEnvelope = Envelope_init_zero;
    size_t _uiEventHead = 0;
    size_t _uiEventTail = 0;
    size_t _uiEventCount = 0;

    static void onBridgeEnvelopeStatic(const Envelope& env, void* userData);
    void onBridgeEnvelope(const Envelope& env);

    static bool onUiEventStatic(const Envelope& env, void* userData);
    bool onUiEvent(const Envelope& env);

    bool enqueueUiEvent(const Envelope& env);
    bool flushUiEvents();

    bool sendOutgoingEnvelope(const Envelope& env);
    Envelope& prepareEnvelope(pb_size_t payloadTag);

    static void copyTextSafe(char* dst, size_t dstSize, const char* src);
};

}  // namespace screenlib::client
