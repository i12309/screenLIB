#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "config/ScreenConfig.h"
#include "manager/ScreenManager.h"
#include "link/ITransport.h"
#include "bridge/ScreenBridge.h"

namespace screenlib {

// ScreenSystem — внешний фасад библиотеки для кода приложения.
class ScreenSystem {
public:
    using EventHandler = ScreenManager::EventHandler;

    // Инициализация из уже разобранного типизированного конфига.
    // Может падать, если transport не поддержан в текущей сборке.
    bool init(const ScreenConfig& cfg);

    // Инициализация из JSON-конфига.
    bool initFromJson(const char* json, char* errBuf = nullptr, size_t errBufSize = 0);

    // Главный runtime-тик библиотеки.
    void tick();

    // Transitional API: ручная привязка bridge извне.
    // Если bridge задан вручную, он имеет приоритет над auto-bootstrap.
    void bindPhysicalBridge(ScreenBridge* bridge);
    void bindWebBridge(ScreenBridge* bridge);

    bool connectedPhysical() const;
    bool connectedWeb() const;

    // Текст последней ошибки bootstrap/init.
    const char* lastError() const { return _lastError; }

    void setEventHandler(EventHandler handler, void* userData = nullptr);
    void setPageRegistry(PageRegistry* registry);

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

    // Последняя ошибка инициализации.
    char _lastError[160] = {};

    // Manual bridge (transitional). Если задан, автоподнятый bridge не используется.
    ScreenBridge* _manualPhysicalBridge = nullptr;
    ScreenBridge* _manualWebBridge = nullptr;

    // Owned runtime, который поднимает сам ScreenSystem из ScreenConfig.
    std::unique_ptr<ITransport> _ownedPhysicalTransport;
    std::unique_ptr<ITransport> _ownedWebTransport;
    std::unique_ptr<ScreenBridge> _ownedPhysicalBridge;
    std::unique_ptr<ScreenBridge> _ownedWebBridge;

    bool initWithError(const ScreenConfig& cfg, char* errBuf, size_t errBufSize);
    bool bootstrapRuntime(char* errBuf, size_t errBufSize);
    void clearOwnedRuntime();
    void bindBridgesToManager();
    void setError(const char* text, char* errBuf, size_t errBufSize);
};

}  // namespace screenlib
