#include "ScreenManager.h"

namespace screenlib {

bool ScreenManager::init(const ScreenConfig& cfg) {
    _cfg = cfg;
    _initialized = true;

    // Если endpoint уже были привязаны ранее, просто обновляем флаги активности.
    _physical.setEnabled(_cfg.physical.enabled);
    _web.setEnabled(_cfg.web.enabled);
    return true;
}

void ScreenManager::bindPhysical(ScreenBridge* bridge) {
    _physical.configure(0, EndpointRole::Physical, bridge, _cfg.physical.enabled);
    _physical.setIncomingHandler(&ScreenManager::onEndpointEvent, this);
}

void ScreenManager::bindWeb(ScreenBridge* bridge) {
    _web.configure(1, EndpointRole::Web, bridge, _cfg.web.enabled);
    _web.setIncomingHandler(&ScreenManager::onEndpointEvent, this);
}

void ScreenManager::tick() {
    if (!_initialized) {
        return;
    }

    // Endpoint опрашиваются независимо. Это важно для mirror-режима.
    _physical.tick();
    _web.tick();
}

bool ScreenManager::connectedPhysical() const {
    return _physical.connected();
}

bool ScreenManager::connectedWeb() const {
    return _web.connected();
}

bool ScreenManager::showPage(uint32_t pageId) {
    return sendShowPageByMode(pageId);
}

bool ScreenManager::setText(uint32_t elementId, const char* text) {
    return sendSetTextByMode(elementId, text);
}

bool ScreenManager::setValue(uint32_t elementId, int32_t value) {
    return sendSetValueByMode(elementId, value);
}

bool ScreenManager::setVisible(uint32_t elementId, bool visible) {
    return sendSetVisibleByMode(elementId, visible);
}

bool ScreenManager::sendHeartbeat(uint32_t uptimeMs) {
    return sendHeartbeatByMode(uptimeMs);
}

bool ScreenManager::sendBatch(const SetBatch& batch) {
    return sendBatchByMode(batch);
}

void ScreenManager::onEndpointEvent(const Envelope& env, const ScreenEventContext& ctx, void* userData) {
    ScreenManager* self = static_cast<ScreenManager*>(userData);
    if (self == nullptr) {
        return;
    }
    self->handleEndpointEvent(env, ctx);
}

void ScreenManager::handleEndpointEvent(const Envelope& env, const ScreenEventContext& ctx) {
    // Важно: callback также вызывается синхронно из tick().
    if (_eventHandler != nullptr) {
        _eventHandler(env, ctx, _eventUser);
    }
}

MirrorMode ScreenManager::effectiveMode() const {
    return _cfg.mirrorMode;
}

bool ScreenManager::sendShowPageByMode(uint32_t pageId) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.showPage(pageId);
        case MirrorMode::WebOnly:
            return _web.showPage(pageId);
        case MirrorMode::Both: {
            const bool okPhysical = _physical.showPage(pageId);
            const bool okWeb = _web.showPage(pageId);
            // Для mirror считаем успехом отправку хотя бы в один живой endpoint.
            return okPhysical || okWeb;
        }
    }
    return false;
}

bool ScreenManager::sendSetTextByMode(uint32_t elementId, const char* text) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.setText(elementId, text);
        case MirrorMode::WebOnly:
            return _web.setText(elementId, text);
        case MirrorMode::Both: {
            const bool okPhysical = _physical.setText(elementId, text);
            const bool okWeb = _web.setText(elementId, text);
            return okPhysical || okWeb;
        }
    }
    return false;
}

bool ScreenManager::sendSetValueByMode(uint32_t elementId, int32_t value) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.setValue(elementId, value);
        case MirrorMode::WebOnly:
            return _web.setValue(elementId, value);
        case MirrorMode::Both: {
            const bool okPhysical = _physical.setValue(elementId, value);
            const bool okWeb = _web.setValue(elementId, value);
            return okPhysical || okWeb;
        }
    }
    return false;
}

bool ScreenManager::sendSetVisibleByMode(uint32_t elementId, bool visible) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.setVisible(elementId, visible);
        case MirrorMode::WebOnly:
            return _web.setVisible(elementId, visible);
        case MirrorMode::Both: {
            const bool okPhysical = _physical.setVisible(elementId, visible);
            const bool okWeb = _web.setVisible(elementId, visible);
            return okPhysical || okWeb;
        }
    }
    return false;
}

bool ScreenManager::sendHeartbeatByMode(uint32_t uptimeMs) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.sendHeartbeat(uptimeMs);
        case MirrorMode::WebOnly:
            return _web.sendHeartbeat(uptimeMs);
        case MirrorMode::Both: {
            const bool okPhysical = _physical.sendHeartbeat(uptimeMs);
            const bool okWeb = _web.sendHeartbeat(uptimeMs);
            return okPhysical || okWeb;
        }
    }
    return false;
}

bool ScreenManager::sendBatchByMode(const SetBatch& batch) {
    switch (effectiveMode()) {
        case MirrorMode::PhysicalOnly:
            return _physical.sendBatch(batch);
        case MirrorMode::WebOnly:
            return _web.sendBatch(batch);
        case MirrorMode::Both: {
            // Важно: split-batch внутри ScreenBridge не атомарен.
            // При mirror это может дать частичную доставку на одном из endpoint.
            const bool okPhysical = _physical.sendBatch(batch);
            const bool okWeb = _web.sendBatch(batch);
            return okPhysical || okWeb;
        }
    }
    return false;
}

}  // namespace screenlib

