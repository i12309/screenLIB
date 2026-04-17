#include "ScreenSystem.h"

#include <stdio.h>
#include <string.h>

#include "config/ScreenConfigJson.h"
#include "link/WebSocketServerLink.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "link/UartLink.h"
#endif

namespace screenlib {

bool ScreenSystem::init(const ScreenConfig& cfg) {
    return initWithError(cfg, nullptr, 0);
}

bool ScreenSystem::initFromJson(const char* json, char* errBuf, size_t errBufSize) {
    ScreenConfig parsed{};
    if (!ScreenConfigJson::parse(json, parsed, errBuf, errBufSize)) {
        if (errBuf != nullptr && errBufSize > 0 && errBuf[0] != '\0') {
            setError(errBuf, errBuf, errBufSize);
        } else {
            setError("config parse failed", errBuf, errBufSize);
        }
        return false;
    }
    return initWithError(parsed, errBuf, errBufSize);
}

void ScreenSystem::tick() {
    if (!_initialized) {
        return;
    }
    _manager.tick();
}

void ScreenSystem::bindPhysicalBridge(ScreenBridge* bridge) {
    _manualPhysicalBridge = bridge;
    if (_initialized) {
        bindBridgesToManager();
    }
}

void ScreenSystem::bindWebBridge(ScreenBridge* bridge) {
    _manualWebBridge = bridge;
    if (_initialized) {
        bindBridgesToManager();
    }
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

bool ScreenSystem::requestDeviceInfo(uint32_t requestId) {
    return _manager.requestDeviceInfo(requestId);
}

bool ScreenSystem::requestCurrentPage(uint32_t requestId) {
    return _manager.requestCurrentPage(requestId);
}

bool ScreenSystem::requestPageState(uint32_t pageId, uint32_t requestId) {
    return _manager.requestPageState(pageId, requestId);
}

bool ScreenSystem::initWithError(const ScreenConfig& cfg, char* errBuf, size_t errBufSize) {
    _cfg = cfg;
    _initialized = false;

    if (!bootstrapRuntime(errBuf, errBufSize)) {
        return false;
    }

    if (!_manager.init(_cfg)) {
        setError("manager init failed", errBuf, errBufSize);
        return false;
    }

    bindBridgesToManager();
    setError("", errBuf, errBufSize);
    _initialized = true;
    return true;
}

bool ScreenSystem::bootstrapRuntime(char* errBuf, size_t errBufSize) {
    clearOwnedRuntime();

    // Physical output bootstrap.
    if (_cfg.physical.enabled) {
        switch (_cfg.physical.type) {
            case OutputType::Uart: {
#ifdef ARDUINO
                // По умолчанию поднимаем physical UART на Serial1.
                std::unique_ptr<UartLink> uart(new UartLink(Serial1));
                UartLink::Config uartCfg{};
                uartCfg.baud = _cfg.physical.uart.baud;
                uartCfg.rxPin = _cfg.physical.uart.rxPin;
                uartCfg.txPin = _cfg.physical.uart.txPin;
                uart->begin(uartCfg);
                _ownedPhysicalTransport = std::move(uart);
                _ownedPhysicalBridge.reset(new ScreenBridge(*_ownedPhysicalTransport));
#else
                setError("physical uart requires ARDUINO build", errBuf, errBufSize);
                return false;
#endif
                break;
            }
            case OutputType::WsServer: {
                std::unique_ptr<WebSocketServerLink> ws(new WebSocketServerLink(_cfg.physical.wsServer.port));
                ws->begin();
                _ownedPhysicalTransport = std::move(ws);
                _ownedPhysicalBridge.reset(new ScreenBridge(*_ownedPhysicalTransport));
                break;
            }
            case OutputType::WsClient:
                setError("ws_client is client-side transport", errBuf, errBufSize);
                return false;
            case OutputType::None:
            default:
                setError("physical output type is not set", errBuf, errBufSize);
                return false;
        }
    }

    // Web output bootstrap.
    if (_cfg.web.enabled) {
        switch (_cfg.web.type) {
            case OutputType::WsServer: {
                std::unique_ptr<WebSocketServerLink> ws(new WebSocketServerLink(_cfg.web.wsServer.port));
                ws->begin();
                _ownedWebTransport = std::move(ws);
                _ownedWebBridge.reset(new ScreenBridge(*_ownedWebTransport));
                break;
            }
            case OutputType::Uart: {
#ifdef ARDUINO
                // Для web-слота UART пока поддерживаем как тех-режим через Serial2.
                std::unique_ptr<UartLink> uart(new UartLink(Serial2));
                UartLink::Config uartCfg{};
                uartCfg.baud = _cfg.web.uart.baud;
                uartCfg.rxPin = _cfg.web.uart.rxPin;
                uartCfg.txPin = _cfg.web.uart.txPin;
                uart->begin(uartCfg);
                _ownedWebTransport = std::move(uart);
                _ownedWebBridge.reset(new ScreenBridge(*_ownedWebTransport));
#else
                setError("web uart requires ARDUINO build", errBuf, errBufSize);
                return false;
#endif
                break;
            }
            case OutputType::WsClient:
                setError("ws_client is client-side transport", errBuf, errBufSize);
                return false;
            case OutputType::None:
            default:
                setError("web output type is not set", errBuf, errBufSize);
                return false;
        }
    }

    return true;
}

void ScreenSystem::clearOwnedRuntime() {
    _ownedPhysicalBridge.reset();
    _ownedWebBridge.reset();
    _ownedPhysicalTransport.reset();
    _ownedWebTransport.reset();
}

void ScreenSystem::bindBridgesToManager() {
    // Приоритет у transitional manual API.
    ScreenBridge* physical = _manualPhysicalBridge != nullptr
                                 ? _manualPhysicalBridge
                                 : _ownedPhysicalBridge.get();
    ScreenBridge* web = _manualWebBridge != nullptr
                            ? _manualWebBridge
                            : _ownedWebBridge.get();

    _manager.bindPhysical(physical);
    _manager.bindWeb(web);
}

void ScreenSystem::setError(const char* text, char* errBuf, size_t errBufSize) {
    const char* src = (text != nullptr) ? text : "";
    snprintf(_lastError, sizeof(_lastError), "%s", src);

    if (errBuf != nullptr && errBufSize > 0) {
        snprintf(errBuf, errBufSize, "%s", src);
    }
}

}  // namespace screenlib
