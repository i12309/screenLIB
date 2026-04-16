#include "WebSocketClientLink.h"

#ifdef ARDUINO

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool WebSocketClientLink::begin(const char* url) {
    char host[96] = {};
    char path[96] = {};
    uint16_t port = 0;

    if (!parseUrl(url, host, sizeof(host), port, path, sizeof(path))) {
        return false;
    }
    return begin(host, port, path);
}

bool WebSocketClientLink::begin(const char* host, uint16_t port, const char* path) {
    if (host == nullptr || host[0] == '\0' || port == 0) {
        return false;
    }
    if (path == nullptr || path[0] == '\0') {
        path = "/";
    }

    _connected = false;
    rxClear();

    _ws.begin(host, port, path);
    _ws.setReconnectInterval(_cfg.reconnectIntervalMs);
    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        this->onEvent(type, payload, length);
    });

    return true;
}

void WebSocketClientLink::onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            rxClear();
            break;

        case WStype_DISCONNECTED:
            _connected = false;
            rxClear();
            break;

        case WStype_BIN:
            if (!_connected || payload == nullptr || length == 0) {
                break;
            }
            for (size_t i = 0; i < length; ++i) {
                rxPush(payload[i]);
            }
            break;

        default:
            break;
    }
}

bool WebSocketClientLink::parseUrl(const char* url,
                                   char* hostOut,
                                   size_t hostOutSize,
                                   uint16_t& portOut,
                                   char* pathOut,
                                   size_t pathOutSize) {
    if (url == nullptr || hostOut == nullptr || pathOut == nullptr ||
        hostOutSize == 0 || pathOutSize == 0) {
        return false;
    }

    hostOut[0] = '\0';
    pathOut[0] = '\0';
    portOut = 0;

    const char* p = url;
    if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else if (strncmp(p, "wss://", 6) == 0) {
        // В текущей версии поддерживаем только ws://.
        return false;
    }

    const char* slash = strchr(p, '/');
    const char* hostPortEnd = (slash != nullptr) ? slash : (p + strlen(p));
    if (hostPortEnd <= p) {
        return false;
    }

    const char* colon = nullptr;
    for (const char* it = p; it < hostPortEnd; ++it) {
        if (*it == ':') {
            colon = it;
            break;
        }
    }
    if (colon == nullptr) {
        return false;
    }

    const size_t hostLen = static_cast<size_t>(colon - p);
    if (hostLen == 0 || hostLen >= hostOutSize) {
        return false;
    }
    memcpy(hostOut, p, hostLen);
    hostOut[hostLen] = '\0';

    const char* portStart = colon + 1;
    if (portStart >= hostPortEnd) {
        return false;
    }

    char portText[8] = {};
    const size_t portLen = static_cast<size_t>(hostPortEnd - portStart);
    if (portLen == 0 || portLen >= sizeof(portText)) {
        return false;
    }
    memcpy(portText, portStart, portLen);
    portText[portLen] = '\0';
    for (size_t i = 0; i < portLen; ++i) {
        if (!isdigit(static_cast<unsigned char>(portText[i]))) {
            return false;
        }
    }

    const long parsedPort = strtol(portText, nullptr, 10);
    if (parsedPort <= 0 || parsedPort > 65535) {
        return false;
    }
    portOut = static_cast<uint16_t>(parsedPort);

    if (slash != nullptr) {
        strncpy(pathOut, slash, pathOutSize - 1);
        pathOut[pathOutSize - 1] = '\0';
    } else {
        strncpy(pathOut, "/", pathOutSize - 1);
        pathOut[pathOutSize - 1] = '\0';
    }

    return true;
}

#endif

