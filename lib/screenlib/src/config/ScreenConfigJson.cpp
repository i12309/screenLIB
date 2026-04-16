#include "ScreenConfigJson.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace screenlib {
namespace {

// Безопасная запись текста ошибки в буфер.
void writeError(char* errBuf, size_t errBufSize, const char* msg) {
    if (errBuf == nullptr || errBufSize == 0) {
        return;
    }
    errBuf[0] = '\0';
    if (msg == nullptr) {
        return;
    }
    snprintf(errBuf, errBufSize, "%s", msg);
}

// Безопасное копирование строки для фиксированного буфера.
void copyText(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

OutputType parseOutputType(const char* typeText) {
    if (typeText == nullptr) {
        return OutputType::None;
    }
    if (strcmp(typeText, "uart") == 0) {
        return OutputType::Uart;
    }
    if (strcmp(typeText, "ws_server") == 0) {
        return OutputType::WsServer;
    }
    if (strcmp(typeText, "ws_client") == 0) {
        return OutputType::WsClient;
    }
    return OutputType::None;
}

MirrorMode parseMirrorMode(const char* modeText) {
    if (modeText == nullptr) {
        return MirrorMode::PhysicalOnly;
    }
    if (strcmp(modeText, "physical") == 0) {
        return MirrorMode::PhysicalOnly;
    }
    if (strcmp(modeText, "web") == 0) {
        return MirrorMode::WebOnly;
    }
    if (strcmp(modeText, "both") == 0) {
        return MirrorMode::Both;
    }
    return MirrorMode::PhysicalOnly;
}

void parseOutputObject(JsonVariantConst outNode, OutputConfig& out) {
    if (outNode.isNull()) {
        return;
    }

    out.enabled = outNode["enabled"] | out.enabled;
    out.type = parseOutputType(outNode["type"] | nullptr);

    out.uart.baud = outNode["baud"] | out.uart.baud;
    out.uart.rxPin = outNode["rxPin"] | out.uart.rxPin;
    out.uart.txPin = outNode["txPin"] | out.uart.txPin;

    out.wsServer.port = outNode["port"] | out.wsServer.port;

    const char* url = outNode["url"] | nullptr;
    copyText(out.wsClient.url, sizeof(out.wsClient.url), url);
}

}  // namespace

bool ScreenConfigJson::parse(const char* json,
                             ScreenConfig& out,
                             char* errBuf,
                             size_t errBufSize) {
    if (json == nullptr || json[0] == '\0') {
        writeError(errBuf, errBufSize, "empty json");
        return false;
    }

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(2048);
#endif

    const DeserializationError err = deserializeJson(doc, json);
    if (err) {
        writeError(errBuf, errBufSize, err.c_str());
        return false;
    }

    ScreenConfig parsed{};

    const JsonVariantConst outputs = doc["outputs"];
    parseOutputObject(outputs["physical"], parsed.physical);
    parseOutputObject(outputs["web"], parsed.web);

    // Поддержка двух схем:
    // 1) mirror: true/false (наследие идеи)
    // 2) routing.defaultTarget: "physical" | "web" | "both"
    if (!doc["routing"]["defaultTarget"].isNull()) {
        parsed.mirrorMode = parseMirrorMode(doc["routing"]["defaultTarget"] | nullptr);
    } else {
        const bool mirror = doc["mirror"] | false;
        parsed.mirrorMode = mirror ? MirrorMode::Both : MirrorMode::PhysicalOnly;
    }

    out = parsed;
    writeError(errBuf, errBufSize, "");
    return true;
}

}  // namespace screenlib

