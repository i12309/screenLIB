#pragma once

#include <stdint.h>

#include "types/ScreenTypes.h"

namespace screenlib {

// Конфиг UART-вывода (физический экран).
struct UartConfig {
    uint32_t baud = 115200;
    int8_t rxPin = -1;
    int8_t txPin = -1;
};

// Конфиг WebSocket server-вывода (ESP32 как сервер).
struct WsServerConfig {
    uint16_t port = 81;
};

// Конфиг WebSocket client-вывода (браузер/WASM как клиент), позже.
struct WsClientConfig {
    static constexpr uint16_t kMaxUrlLen = 128;
    char url[kMaxUrlLen] = {0};
};

// Универсальный конфиг одного выхода.
struct OutputConfig {
    bool enabled = false;
    OutputType type = OutputType::None;
    UartConfig uart;
    WsServerConfig wsServer;
    WsClientConfig wsClient;
};

// Общий конфиг библиотеки экранов.
struct ScreenConfig {
    OutputConfig physical;
    OutputConfig web;
    MirrorMode mirrorMode = MirrorMode::PhysicalOnly;
};

}  // namespace screenlib
