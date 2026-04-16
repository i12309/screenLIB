#pragma once

#include <Arduino.h>
#include "link/ITransport.h"

// ============================================================
// UartLink — реализация ITransport поверх HardwareSerial.
// Не знает ничего про framing, protobuf и UI.
// ============================================================

class UartLink : public ITransport {
public:
    struct Config {
        uint32_t baud    = 115200;
        int8_t   rxPin   = -1;   // -1 = использовать дефолтные пины
        int8_t   txPin   = -1;
    };

    explicit UartLink(HardwareSerial& serial) : _serial(serial) {}

    // Инициализация с явными пинами
    void begin(const Config& cfg) {
        if (cfg.rxPin >= 0 && cfg.txPin >= 0) {
            _serial.begin(cfg.baud, SERIAL_8N1, cfg.rxPin, cfg.txPin);
        } else {
            _serial.begin(cfg.baud);
        }
        _ready = true;
    }

    // Инициализация с дефолтными пинами
    void begin(uint32_t baud = 115200) {
        _serial.begin(baud);
        _ready = true;
    }

    // ----- ITransport -----

    bool connected() const override {
        return _ready;
    }

    // Отправить len байт.
    // Пишет в TX-буфер UART. Может блокироваться если буфер переполнен —
    // на практике не критично при умеренном трафике (кадры небольшие).
    bool write(const uint8_t* data, size_t len) override {
        if (!_ready) return false;
        return _serial.write(data, len) == len;
    }

    // Прочитать до max_len байт из RX-буфера. Возвращает реальное кол-во.
    size_t read(uint8_t* dst, size_t max_len) override {
        if (!_ready) return 0;
        size_t n = 0;
        while (n < max_len && _serial.available() > 0) {
            dst[n++] = static_cast<uint8_t>(_serial.read());
        }
        return n;
    }

    // Для UART tick() не нужен — UART сам пополняет буфер через прерывания.
    void tick() override {}

private:
    HardwareSerial& _serial;
    bool _ready = false;
};
