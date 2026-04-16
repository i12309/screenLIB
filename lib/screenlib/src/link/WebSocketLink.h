#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ITransport.h"

#ifdef ARDUINO

#include <WebSocketsServer.h>

// ============================================================
// WebSocketLink — серверный transport поверх WebSocketsServer.
// Используется на ESP32 (экранный контроллер поднимает WS-сервер).
// ============================================================
static constexpr size_t kWsRxBufSize = 1024;
static_assert((kWsRxBufSize & (kWsRxBufSize - 1)) == 0,
              "kWsRxBufSize must be a power of 2");

class WebSocketLink : public ITransport {
public:
    explicit WebSocketLink(uint16_t port = 81) : _ws(port) {}

    void begin() {
        s_instance = this;
        _ws.begin();
        _ws.onEvent(staticEventHandler);
    }

    bool connected() const override {
        return _clientId >= 0;
    }

    bool write(const uint8_t* data, size_t len) override {
        if (_clientId < 0) return false;
        return _ws.sendBIN(static_cast<uint8_t>(_clientId), data, len);
    }

    size_t read(uint8_t* dst, size_t max_len) override {
        size_t n = 0;
        while (n < max_len && !rxEmpty()) {
            dst[n++] = rxPop();
        }
        return n;
    }

    void tick() override {
        _ws.loop();
    }

private:
    WebSocketsServer _ws;
    int16_t _clientId = -1;

    uint8_t _rxBuf[kWsRxBufSize] = {};
    size_t _rxHead = 0;
    size_t _rxTail = 0;

    bool rxFull() const { return ((_rxHead + 1) & (kWsRxBufSize - 1)) == _rxTail; }
    bool rxEmpty() const { return _rxHead == _rxTail; }

    void rxPush(uint8_t b) {
        if (rxFull()) return;
        _rxBuf[_rxHead] = b;
        _rxHead = (_rxHead + 1) & (kWsRxBufSize - 1);
    }

    uint8_t rxPop() {
        const uint8_t b = _rxBuf[_rxTail];
        _rxTail = (_rxTail + 1) & (kWsRxBufSize - 1);
        return b;
    }

    void rxClear() {
        _rxHead = _rxTail = 0;
    }

    static void staticEventHandler(uint8_t id, WStype_t type, uint8_t* payload, size_t length) {
        if (s_instance) s_instance->onEvent(id, type, payload, length);
    }

    void onEvent(uint8_t id, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
                if (_clientId >= 0 && _clientId != static_cast<int16_t>(id)) {
                    _ws.disconnect(static_cast<uint8_t>(_clientId));
                }
                _clientId = static_cast<int16_t>(id);
                rxClear();
                break;

            case WStype_DISCONNECTED:
                if (_clientId == static_cast<int16_t>(id)) {
                    _clientId = -1;
                    rxClear();
                }
                break;

            case WStype_BIN:
                if (_clientId != static_cast<int16_t>(id)) break;
                for (size_t i = 0; i < length; i++) {
                    rxPush(payload[i]);
                }
                break;

            default:
                break;
        }
    }

    static WebSocketLink* s_instance;
};

#else

// ============================================================
// Заглушка для host/native тестов.
// На non-Arduino платформах WS transport не активен.
// ============================================================
class WebSocketLink : public ITransport {
public:
    explicit WebSocketLink(uint16_t port = 81) : _port(port) {}

    void begin() {}

    bool connected() const override { return false; }
    bool write(const uint8_t* data, size_t len) override {
        (void)data;
        (void)len;
        return false;
    }
    size_t read(uint8_t* dst, size_t max_len) override {
        (void)dst;
        (void)max_len;
        return 0;
    }
    void tick() override {}

private:
    uint16_t _port = 81;
};

#endif

