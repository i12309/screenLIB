#include "ScreenSystem.h"

#include "../config/ScreenConfigJson.h"

namespace screenlib {

bool ScreenSystem::init(const ScreenConfig& cfg) {
    _cfg = cfg;
    _initialized = _manager.init(_cfg);
    return _initialized;
}

bool ScreenSystem::initFromJson(const char* json, char* errBuf, size_t errBufSize) {
    ScreenConfig parsed{};
    if (!ScreenConfigJson::parse(json, parsed, errBuf, errBufSize)) {
        return false;
    }
    return init(parsed);
}

void ScreenSystem::tick() {
    if (!_initialized) {
        return;
    }
    _manager.tick();
}

void ScreenSystem::bindPhysicalBridge(ScreenBridge* bridge) {
    _manager.bindPhysical(bridge);
}

void ScreenSystem::bindWebBridge(ScreenBridge* bridge) {
    _manager.bindWeb(bridge);
}

bool ScreenSystem::connectedPhysical() const {
    return _manager.connectedPhysical();
}

bool ScreenSystem::connectedWeb() const {
    return _manager.connectedWeb();
}

void ScreenSystem::setEventHandler(EventHandler handler, void* userData) {
    _manager.setEventHandler(handler, userData);
}

bool ScreenSystem::showPage(uint32_t pageId) {
    return _manager.showPage(pageId);
}

bool ScreenSystem::setText(uint32_t elementId, const char* text) {
    return _manager.setText(elementId, text);
}

bool ScreenSystem::setValue(uint32_t elementId, int32_t value) {
    return _manager.setValue(elementId, value);
}

bool ScreenSystem::setVisible(uint32_t elementId, bool visible) {
    return _manager.setVisible(elementId, visible);
}

bool ScreenSystem::sendHeartbeat(uint32_t uptimeMs) {
    return _manager.sendHeartbeat(uptimeMs);
}

bool ScreenSystem::sendBatch(const SetBatch& batch) {
    return _manager.sendBatch(batch);
}

}  // namespace screenlib

