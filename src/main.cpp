#include <Arduino.h>

#include "system/ScreenSystem.h"

namespace {

// Пример JSON-конфига для библиотеки.
// Bootstrap transport/bridge выполняет сам ScreenSystem.
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

screenlib::ScreenSystem gScreens;

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

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    char errBuf[160] = {};
    if (!gScreens.initFromJson(kScreenConfigJson, errBuf, sizeof(errBuf))) {
        Serial.printf("[screen] initFromJson failed: %s\n", errBuf);
        return;
    }

    gScreens.setEventHandler(&onScreenEvent, nullptr);

    gScreens.showPage(1);
    gScreens.setText(100, "screenLIB started");

    Serial.println("[screen] setup done");
}

void loop() {
    gScreens.tick();

    const uint32_t now = millis();

    if (now - gLastHeartbeatMs >= 1000) {
        gLastHeartbeatMs = now;
        gScreens.sendHeartbeat(now);
    }

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

