#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ITransport.h"

#ifdef ARDUINO

#include <WebSocketsClient.h>

// ============================================================
// WebSocketClientLink — клиентский transport поверх WebSocketsClient.
// Используется для web/WASM-клиента, который сам подключается к WS-серверу.
// ============================================================
static constexpr size_t kWsClientRxBufSize = 1024;
static_assert((kWsClientRxBufSize & (kWsClientRxBufSize - 1)) == 0,
              "kWsClientRxBufSize must be a power of 2");

class WebSocketClientLink : public ITransport {
public:
    struct Config {
        uint32_t reconnectIntervalMs = 2000;
    };

    WebSocketClientLink() = default;
    explicit WebSocketClientLink(const Config& cfg) : _cfg(cfg) {}

    // Инициализация клиента по URL формата:
    // - ws://host:port/path
    // - host:port/path
    bool begin(const char* url);

    // Инициализация клиента по частям.
    bool begin(const char* host, uint16_t port, const char* path = "/");

    bool connected() const override { return _connected; }

    bool write(const uint8_t* data, size_t len) override {
        if (!_connected || data == nullptr || len == 0) {
            return false;
        }
        return _ws.sendBIN(data, len);
    }

    size_t read(uint8_t* dst, size_t max_len) override {
        if (dst == nullptr || max_len == 0) {
            return 0;
        }

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
    Config _cfg{};
    WebSocketsClient _ws;
    bool _connected = false;

    uint8_t _rxBuf[kWsClientRxBufSize] = {};
    size_t _rxHead = 0;
    size_t _rxTail = 0;

    bool rxFull() const { return ((_rxHead + 1) & (kWsClientRxBufSize - 1)) == _rxTail; }
    bool rxEmpty() const { return _rxHead == _rxTail; }

    void rxPush(uint8_t b) {
        if (rxFull()) {
            return;
        }
        _rxBuf[_rxHead] = b;
        _rxHead = (_rxHead + 1) & (kWsClientRxBufSize - 1);
    }

    uint8_t rxPop() {
        const uint8_t b = _rxBuf[_rxTail];
        _rxTail = (_rxTail + 1) & (kWsClientRxBufSize - 1);
        return b;
    }

    void rxClear() {
        _rxHead = 0;
        _rxTail = 0;
    }

    void onEvent(WStype_t type, uint8_t* payload, size_t length);
    static bool parseUrl(const char* url,
                         char* hostOut,
                         size_t hostOutSize,
                         uint16_t& portOut,
                         char* pathOut,
                         size_t pathOutSize);
};

#else

// ============================================================
// Заглушка для host/native сборок.
// Нужна, чтобы WsClient можно было инициализировать в тестах без железа.
// ============================================================
class WebSocketClientLink : public ITransport {
public:
    struct Config {
        uint32_t reconnectIntervalMs = 2000;
    };

    WebSocketClientLink() = default;
    explicit WebSocketClientLink(const Config& cfg) : _cfg(cfg) {}

    bool begin(const char* url) {
        _ready = (url != nullptr && url[0] != '\0');
        return _ready;
    }

    bool begin(const char* host, uint16_t port, const char* path = "/") {
        (void)port;
        (void)path;
        _ready = (host != nullptr && host[0] != '\0');
        return _ready;
    }

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
    Config _cfg{};
    bool _ready = false;
};

#endif
