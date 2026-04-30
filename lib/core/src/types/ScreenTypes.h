#pragma once

#include <stdint.h>

#define SCREENLIB_CAP_PAGE_TRANSACTION (1u << 0)

namespace screenlib {

// Тип выхода для конкретного endpoint.
enum class OutputType : uint8_t {
    None = 0,
    Uart,
    WsServer,
    WsClient
};

// Режим маршрутизации команд экрана.
enum class MirrorMode : uint8_t {
    PhysicalOnly = 0,
    WebOnly,
    Both
};

// Роль endpoint внутри системы экранов.
enum class EndpointRole : uint8_t {
    Physical = 0,
    Web,
    Aux
};

// Контекст входящего события от экрана.
struct ScreenEventContext {
    uint8_t endpointId = 0;
    bool isPhysical = false;
    bool isWeb = false;
};

}  // namespace screenlib

