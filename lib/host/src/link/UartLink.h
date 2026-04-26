#pragma once

#include <Arduino.h>
#include "link/ITransport.h"
#include "log/ScreenLibLogger.h"

// ============================================================
// UartLink — host-side реализация ITransport поверх HardwareSerial.
// Не знает ничего про framing, protobuf и UI.
// ============================================================

class UartLink : public ITransport {
public:
    static constexpr size_t kRxBufferSize = 512;

    struct Config {
        uint32_t baud = 115200;
        int8_t rxPin = -1;  // -1 = использовать дефолтные пины
        int8_t txPin = -1;
    };

    explicit UartLink(HardwareSerial& serial) : _serial(serial) {}

    // Инициализация с явными пинами.
    void begin(const Config& cfg) {
        _serial.setRxBufferSize(kRxBufferSize);
        if (cfg.rxPin >= 0 && cfg.txPin >= 0) {
            _serial.begin(cfg.baud, SERIAL_8N1, cfg.rxPin, cfg.txPin);
        } else {
            _serial.begin(cfg.baud);
        }
        _ready = true;
        SCREENLIB_LOGI("screenlib.uart",
                       "begin baud=%lu rx=%d tx=%d explicit_pins=%d",
                       static_cast<unsigned long>(cfg.baud),
                       static_cast<int>(cfg.rxPin),
                       static_cast<int>(cfg.txPin),
                       (cfg.rxPin >= 0 && cfg.txPin >= 0) ? 1 : 0);
    }

    // Инициализация с дефолтными пинами.
    void begin(uint32_t baud = 115200) {
        _serial.setRxBufferSize(kRxBufferSize);
        _serial.begin(baud);
        _ready = true;
        SCREENLIB_LOGI("screenlib.uart",
                       "begin baud=%lu default_pins=1",
                       static_cast<unsigned long>(baud));
    }

    bool connected() const override {
        return _ready;
    }

    // Отправить len байт в TX-буфер UART.
    bool write(const uint8_t* data, size_t len) override {
        if (!_ready) return false;
        return _serial.write(data, len) == len;
    }

    // Прочитать до max_len байт из RX-буфера UART.
    size_t read(uint8_t* dst, size_t max_len) override {
        if (!_ready) return 0;
        size_t n = 0;
        while (n < max_len && _serial.available() > 0) {
            dst[n++] = static_cast<uint8_t>(_serial.read());
        }
        return n;
    }

    // Для UART tick() не требуется.
    void tick() override {}

private:
    HardwareSerial& _serial;
    bool _ready = false;
};
