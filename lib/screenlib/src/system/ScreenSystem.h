#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../config/ScreenConfig.h"
#include "../manager/ScreenManager.h"

namespace screenlib {

// ScreenSystem — внешний фасад библиотеки для кода приложения.
class ScreenSystem {
public:
    using EventHandler = ScreenManager::EventHandler;

    // Инициализация из уже разобранного типизированного конфига.
    bool init(const ScreenConfig& cfg);

    // Инициализация из JSON-конфига.
    bool initFromJson(const char* json, char* errBuf = nullptr, size_t errBufSize = 0);

    // Главный runtime-тик библиотеки.
    void tick();

    // На текущем этапе bridge привязываются снаружи.
    // Это сознательно: не смешиваем фасад и фабрику транспортов в одном шаге.
    void bindPhysicalBridge(ScreenBridge* bridge);
    void bindWebBridge(ScreenBridge* bridge);

    bool connectedPhysical() const;
    bool connectedWeb() const;

    void setEventHandler(EventHandler handler, void* userData = nullptr);

    // High-level API для бизнес-логики.
    bool showPage(uint32_t pageId);
    bool setText(uint32_t elementId, const char* text);
    bool setValue(uint32_t elementId, int32_t value);
    bool setVisible(uint32_t elementId, bool visible);
    bool sendHeartbeat(uint32_t uptimeMs);
    bool sendBatch(const SetBatch& batch);

private:
    ScreenConfig _cfg{};
    bool _initialized = false;
    ScreenManager _manager;
};

}  // namespace screenlib

