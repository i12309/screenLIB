#pragma once

#include <stddef.h>
#include <stdint.h>

#include "frame/FrameCodec.h"
#include "link/ITransport.h"
#include "proto/ProtoCodec.h"

// ============================================================
// ScreenBridge — транспортный message-level слой.
// Основа: ITransport + FrameCodec + ProtoCodec.
//
// Задачи:
// - отправка Envelope в канал;
// - прием/декод входящих Envelope;
// - обработчик для входящих сообщений;
// - вспомогательные методы по page_id / element_id.
//
// Важно: слой не зависит от LVGL/EEZ и UI-логики.
// ============================================================
class ScreenBridge {
public:
    // Обработчик для всех входящих Envelope.
    // userData передается без изменений из setEnvelopeHandler().
    using EnvelopeHandler = void (*)(const Envelope& env, void* userData);

    explicit ScreenBridge(ITransport& transport) : _transport(transport) {}

    // Отправить готовый Envelope в транспорт.
    bool sendEnvelope(const Envelope& env);
    // Прочитать транспорт, декодировать кадры и вызвать handler.
    // Возвращает количество успешно обработанных Envelope.
    size_t processIncoming();
    // Короткий синоним для processIncoming().
    size_t poll() { return processIncoming(); }

    // Установить обработчик входящих сообщений.
    void setEnvelopeHandler(EnvelopeHandler handler, void* userData = nullptr) {
        _handler = handler;
        _handlerUser = userData;
    }

    // Текущее состояние канала на уровне транспорта.
    bool connected() const { return _transport.connected(); }

    // Базовые UI-команды (сторона host -> сторона screen).
    bool showPage(uint32_t pageId, uint32_t sessionId = 0);
    bool sendHeartbeat(uint32_t uptimeMs);

    // Типизированные атрибуты UI (сторона host -> сторона screen).
    // С безопасной проверкой соответствия attribute <-> value.
    bool setElementAttribute(const SetElementAttribute& attr);
    // Запрос одного типизированного атрибута у клиента экрана.
    bool requestElementAttribute(uint32_t elementId,
                                 ElementAttribute attribute,
                                 uint32_t pageId = 0,
                                 uint32_t requestId = 0);

    // Сервисные вспомогательные методы запросов (обычно host -> screen).
    bool sendHello(const DeviceInfo& deviceInfo);
    bool requestDeviceInfo(uint32_t requestId = 0);
    bool requestCurrentPage(uint32_t requestId = 0);

    // Сервисные вспомогательные методы ответов (обычно screen -> host).
    bool sendDeviceInfo(const DeviceInfo& deviceInfo);
    bool sendCurrentPage(uint32_t pageId, uint32_t requestId = 0);
    // Ответ на request_element_attribute.
    bool sendElementAttributeState(const ElementAttributeState& state);

private:
    static constexpr size_t kReadChunkSize = 256;
    ITransport& _transport;
    FrameCodec _frameCodec;
    uint8_t _txSeq = 0;
    bool _lastConnected = false;

    EnvelopeHandler _handler = nullptr;
    void* _handlerUser = nullptr;

    uint8_t _readBuf[kReadChunkSize] = {};
    uint8_t _protoTxBuf[ProtoCodec::kMaxEncodedSize] = {};
    uint8_t _frameTxBuf[kMaxPayload + kFrameOverhead] = {};
    Envelope _txEnvelope = {};
    Envelope _rxEnvelope = {};

    bool sendFramePayload(const uint8_t* payload, size_t payloadLen);
    void dispatchEnvelope(const Envelope& env) const;
    Envelope& prepareTxEnvelope(pb_size_t payloadTag);

    // Валидация/нормализация типизированных атрибутов.
    static bool sanitizeElementAttribute(const SetElementAttribute& src, SetElementAttribute& dst);
};
