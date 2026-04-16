#pragma once

#include <WebSocketsServer.h>
#include "ITransport.h"

// ============================================================
// WebSocketLink — реализация ITransport поверх WebSocketsServer.
// Входящие WS BIN-фреймы накапливаются в RX ring buffer,
// чтобы верхние слои читали байтовый поток без знания о WS.
//
// Зависимость: arduinoWebSockets (Markus Sattler).
//
// Ограничение — один экземпляр на программу. Осознанное:
// библиотека требует статический callback без контекста,
// поэтому храним глобальный s_instance. Больше одного экрана
// по WebSocket нам не нужно.
//
// Это серверный транспорт (ESP32 слушает, браузер подключается).
// Для браузерного WASM нужен отдельный WebSocketClientLink : ITransport,
// который сам подключается к серверу. Реализуется позже отдельным файлом.
//
// Один клиент за раз: при новом подключении старый отключается явно.
// RX buffer очищается при connect/disconnect — не тащим чужие хвосты.
// ============================================================

static constexpr size_t kWsRxBufSize = 1024;

// Compile-time: ring buffer работает корректно только на степени двойки
static_assert((kWsRxBufSize & (kWsRxBufSize - 1)) == 0,
              "kWsRxBufSize must be a power of 2");

class WebSocketLink : public ITransport {
public:
    explicit WebSocketLink(uint16_t port = 81) : _ws(port) {}

    // Запуск сервера. Вызывать один раз в setup().
    void begin() {
        // Один экземпляр — присваиваем до регистрации callback
        s_instance = this;
        _ws.begin();
        _ws.onEvent(staticEventHandler);
    }

    // ----- ITransport -----

    // true пока есть активный подключённый клиент
    bool connected() const override {
        return _clientId >= 0;
    }

    // Отправить бинарный WS-фрейм активному клиенту
    bool write(const uint8_t* data, size_t len) override {
        if (_clientId < 0) return false;
        return _ws.sendBIN(static_cast<uint8_t>(_clientId), data, len);
    }

    // Прочитать до max_len байт из RX ring buffer
    size_t read(uint8_t* dst, size_t max_len) override {
        size_t n = 0;
        while (n < max_len && !rxEmpty()) {
            dst[n++] = rxPop();
        }
        return n;
    }

    // Обработка WS событий — вызывать каждый loop()
    void tick() override {
        _ws.loop();
    }

private:
    WebSocketsServer _ws;
    int16_t _clientId = -1;  // -1 = нет клиента

    // --- RX ring buffer ---
    uint8_t _rxBuf[kWsRxBufSize] = {};
    size_t  _rxHead = 0;  // индекс записи
    size_t  _rxTail = 0;  // индекс чтения

    bool rxFull()  const { return ((_rxHead + 1) & (kWsRxBufSize - 1)) == _rxTail; }
    bool rxEmpty() const { return _rxHead == _rxTail; }

    // При переполнении новые байты отбрасываются.
    // Это значит верхний слой не успевает читать — нужно увеличить kWsRxBufSize.
    void rxPush(uint8_t b) {
        if (rxFull()) return;
        _rxBuf[_rxHead] = b;
        _rxHead = (_rxHead + 1) & (kWsRxBufSize - 1);
    }

    uint8_t rxPop() {
        uint8_t b = _rxBuf[_rxTail];
        _rxTail = (_rxTail + 1) & (kWsRxBufSize - 1);
        return b;
    }

    // Сбросить буфер — вызывать при connect/disconnect
    void rxClear() {
        _rxHead = _rxTail = 0;
    }

    // --- WebSocket callback ---

    static void staticEventHandler(uint8_t id, WStype_t type,
                                   uint8_t* payload, size_t length) {
        if (s_instance) s_instance->onEvent(id, type, payload, length);
    }

    void onEvent(uint8_t id, WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
                // Есть старый клиент — явно отключаем перед сменой
                if (_clientId >= 0 && _clientId != static_cast<int16_t>(id)) {
                    _ws.disconnect(static_cast<uint8_t>(_clientId));
                }
                _clientId = static_cast<int16_t>(id);
                rxClear();  // данные старого клиента нерелевантны
                break;

            case WStype_DISCONNECTED:
                if (_clientId == static_cast<int16_t>(id)) {
                    _clientId = -1;
                    rxClear();  // чистим возможный хвост незаконченного кадра
                }
                break;

            case WStype_BIN:
                // Игнорируем байты от чужих клиентов (не должно случаться,
                // но защищаемся на случай гонки при переподключении)
                if (static_cast<int16_t>(id) != _clientId) break;
                for (size_t i = 0; i < length; i++) {
                    rxPush(payload[i]);
                }
                break;

            default:
                // TEXT, PING, PONG и прочее не используем
                break;
        }
    }

    static WebSocketLink* s_instance;  // определение — в WebSocketLink.cpp
};
