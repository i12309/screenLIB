#pragma once

#include <stdint.h>

#include "../config/ScreenConfig.h"
#include "ScreenEndpoint.h"

namespace screenlib {

// ScreenManager — runtime маршрутизация команд и событий между endpoint.
class ScreenManager {
public:
    // Единый обработчик входящих событий от всех экранов.
    using EventHandler = void (*)(const Envelope& env, const ScreenEventContext& ctx, void* userData);

    // Инициализация режимов маршрутизации.
    bool init(const ScreenConfig& cfg);

    // Привязка endpoint к физическому экрану.
    void bindPhysical(ScreenBridge* bridge);
    // Привязка endpoint к web-экрану.
    void bindWeb(ScreenBridge* bridge);

    // Главный runtime-метод.
    void tick();

    bool connectedPhysical() const;
    bool connectedWeb() const;

    // Установить внешний обработчик событий.
    void setEventHandler(EventHandler handler, void* userData = nullptr) {
        _eventHandler = handler;
        _eventUser = userData;
    }

    // Высокоуровневые команды экрана.
    bool showPage(uint32_t pageId);
    bool setText(uint32_t elementId, const char* text);
    bool setValue(uint32_t elementId, int32_t value);
    bool setVisible(uint32_t elementId, bool visible);
    // Точечная типизированная запись одного атрибута элемента.
    bool setElementAttribute(const SetElementAttribute& attr);
    bool sendHeartbeat(uint32_t uptimeMs);
    bool sendBatch(const SetBatch& batch);
    bool requestDeviceInfo(uint32_t requestId = 0);
    bool requestCurrentPage(uint32_t requestId = 0);
    bool requestPageState(uint32_t pageId, uint32_t requestId = 0);
    // Запрос значения одного типизированного атрибута элемента.
    bool requestElementAttribute(uint32_t elementId,
                                 ElementAttribute attribute,
                                 uint32_t pageId = 0,
                                 uint32_t requestId = 0);

private:
    ScreenConfig _cfg{};
    bool _initialized = false;

    ScreenEndpoint _physical;
    ScreenEndpoint _web;

    EventHandler _eventHandler = nullptr;
    void* _eventUser = nullptr;

    // Общий обработчик endpoint -> manager.
    static void onEndpointEvent(const Envelope& env, const ScreenEventContext& ctx, void* userData);
    void handleEndpointEvent(const Envelope& env, const ScreenEventContext& ctx);

    // Определить режим отправки.
    MirrorMode effectiveMode() const;

    // Отправка по текущему режиму.
    bool sendShowPageByMode(uint32_t pageId);
    bool sendSetTextByMode(uint32_t elementId, const char* text);
    bool sendSetValueByMode(uint32_t elementId, int32_t value);
    bool sendSetVisibleByMode(uint32_t elementId, bool visible);
    // Маршрутизация типизированных операций с учетом MirrorMode.
    bool sendSetElementAttributeByMode(const SetElementAttribute& attr);
    bool sendHeartbeatByMode(uint32_t uptimeMs);
    bool sendBatchByMode(const SetBatch& batch);
    bool sendRequestDeviceInfoByMode(uint32_t requestId);
    bool sendRequestCurrentPageByMode(uint32_t requestId);
    bool sendRequestPageStateByMode(uint32_t pageId, uint32_t requestId);
    bool sendRequestElementAttributeByMode(uint32_t elementId,
                                           ElementAttribute attribute,
                                           uint32_t pageId,
                                           uint32_t requestId);
};

}  // namespace screenlib
