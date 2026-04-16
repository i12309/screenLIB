#include <Arduino.h>

#include "config/ScreenConfig.h"
#include "config/ScreenConfigJson.h"
#include "link/UartLink.h"
#include "link/WebSocketLink.h"
#include "screen/ScreenBridge.h"
#include "system/ScreenSystem.h"

namespace {

// Пример JSON-конфига для библиотеки.
// Сейчас включен только web-выход (WS server), physical отключен.
constexpr const char* kScreenConfigJson = R"json(
{
  "outputs": {
    "physical": {
      "enabled": false,
      "type": "uart",
      "baud": 115200,
      "rxPin": 16,
      "txPin": 17
    },
    "web": {
      "enabled": true,
      "type": "ws_server",
      "port": 81
    }
  },
  "routing": {
    "defaultTarget": "both"
  }
}
)json";

HardwareSerial kScreenUart(1);

UartLink gPhysicalTransport(kScreenUart);
WebSocketLink gWebTransport(81);

ScreenBridge gPhysicalBridge(gPhysicalTransport);
ScreenBridge gWebBridge(gWebTransport);

screenlib::ScreenSystem gScreens;
screenlib::ScreenConfig gConfig;

uint32_t gLastHeartbeatMs = 0;
uint32_t gLastDemoTextMs = 0;
int32_t gCounter = 0;

void onScreenEvent(const Envelope& env, const screenlib::ScreenEventContext& ctx, void* userData) {
    (void)userData;
    Serial.printf("[screen-event] endpoint=%u physical=%d web=%d payload_tag=%u\n",
                  ctx.endpointId,
                  ctx.isPhysical ? 1 : 0,
                  ctx.isWeb ? 1 : 0,
                  static_cast<unsigned>(env.which_payload));
}

void initTransportsFromConfig(const screenlib::ScreenConfig& cfg) {
    if (cfg.physical.enabled && cfg.physical.type == screenlib::OutputType::Uart) {
        UartLink::Config uartCfg{};
        uartCfg.baud = cfg.physical.uart.baud;
        uartCfg.rxPin = cfg.physical.uart.rxPin;
        uartCfg.txPin = cfg.physical.uart.txPin;
        gPhysicalTransport.begin(uartCfg);
        Serial.printf("[screen] physical uart started: baud=%lu rx=%d tx=%d\n",
                      static_cast<unsigned long>(uartCfg.baud),
                      static_cast<int>(uartCfg.rxPin),
                      static_cast<int>(uartCfg.txPin));
    }

    if (cfg.web.enabled && cfg.web.type == screenlib::OutputType::WsServer) {
        gWebTransport.begin();
        Serial.printf("[screen] web ws server started: port=%u\n",
                      static_cast<unsigned>(cfg.web.wsServer.port));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    char errBuf[128] = {};
    if (!screenlib::ScreenConfigJson::parse(kScreenConfigJson, gConfig, errBuf, sizeof(errBuf))) {
        Serial.printf("[screen] config parse failed: %s\n", errBuf);
        return;
    }

    initTransportsFromConfig(gConfig);

    // Инициализируем систему и привязываем endpoint-bridge.
    gScreens.init(gConfig);
    gScreens.bindPhysicalBridge(&gPhysicalBridge);
    gScreens.bindWebBridge(&gWebBridge);
    gScreens.setEventHandler(&onScreenEvent, nullptr);

    // Тестовая команда в UI.
    gScreens.showPage(1);
    gScreens.setText(100, "screenLIB started");

    Serial.println("[screen] setup done");
}

void loop() {
    gScreens.tick();

    const uint32_t now = millis();

    // Периодический heartbeat.
    if (now - gLastHeartbeatMs >= 1000) {
        gLastHeartbeatMs = now;
        gScreens.sendHeartbeat(now);
    }

    // Тестовое обновление элемента раз в 2 секунды.
    if (now - gLastDemoTextMs >= 2000) {
        gLastDemoTextMs = now;
        gCounter++;

        char text[32] = {};
        snprintf(text, sizeof(text), "counter=%ld", static_cast<long>(gCounter));
        gScreens.setText(101, text);
        gScreens.setValue(200, gCounter);
    }

    delay(5);
}
