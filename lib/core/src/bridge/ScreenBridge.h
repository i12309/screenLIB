#pragma once

#include <stddef.h>
#include <stdint.h>

#include "frame/FrameCodec.h"
#include "link/ITransport.h"
#include "proto/ProtoCodec.h"

// ============================================================
// ScreenBridge — высокий message-level слой над байтовым линком.
// Основа: ITransport + FrameCodec + ProtoCodec.
//
// Задачи:
// - отправка Envelope в транспорт;
// - приём/декод входящих Envelope;
// - вызов callback для входящих сообщений;
// - helper-методы уровня page_id / element_id / value.
//
// Важно: здесь нет прямой привязки к LVGL/EEZ.
// ============================================================
class ScreenBridge {
public:
    // Callback для всех входящих сообщений.
    // userData передаётся без изменений из setEnvelopeHandler().
    using EnvelopeHandler = void (*)(const Envelope& env, void* userData);

    explicit ScreenBridge(ITransport& transport) : _transport(transport) {}

    // Отправить готовый Envelope в канал.
    bool sendEnvelope(const Envelope& env);

    // Обработать входящие байты: tick транспорта, parsing frame, decode protobuf.
    // Возвращает количество успешно декодированных и переданных в callback Envelope.
    size_t processIncoming();

    // Короткий alias для цикла приложения.
    size_t poll() { return processIncoming(); }

    // Установить обработчик входящих Envelope.
    void setEnvelopeHandler(EnvelopeHandler handler, void* userData = nullptr) {
        _handler = handler;
        _handlerUser = userData;
    }

    // Состояние физического/логического канала от транспорта.
    bool connected() const { return _transport.connected(); }

    // ----- Тонкие helper-методы -----
    bool showPage(uint32_t pageId);
    bool setText(uint32_t elementId, const char* text);
    bool setValue(uint32_t elementId, int32_t value);
    bool setVisible(uint32_t elementId, bool visible);
    bool sendHeartbeat(uint32_t uptimeMs);
    // Отправка batch:
    // 1) пробуем одним Envelope(SetBatch);
    // 2) если не удалось по ЛЮБОЙ причине, отправляем split по элементам.
    // Это не "только oversized fallback", а общий fallback-режим.
    bool sendBatch(const SetBatch& batch);

    // Service helper-методы.
    bool sendHello(const DeviceInfo& deviceInfo);
    bool requestDeviceInfo(uint32_t requestId = 0);
    bool requestCurrentPage(uint32_t requestId = 0);
    bool requestPageState(uint32_t pageId, uint32_t requestId = 0);
    // page_id опционален: 0 означает "контекстная/текущая страница".
    bool requestElementState(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0);

    bool sendDeviceInfo(const DeviceInfo& deviceInfo);
    bool sendCurrentPage(uint32_t pageId, uint32_t requestId = 0);
    bool sendPageState(const PageState& pageState);
    bool sendElementState(const ElementState& elementState);

private:
    // Размер порции чтения из транспорта за один read().
    static constexpr size_t kReadChunkSize = 256;
    // Прикладной лимит ScreenBridge для безопасной нормализации batch.
    // Это НЕ лимит protobuf-схемы как таковой.
    static constexpr uint8_t kMaxBatchCount = 8;

    ITransport& _transport;
    FrameCodec _frameCodec;
    uint8_t _txSeq = 0;
    bool _lastConnected = false;

    EnvelopeHandler _handler = nullptr;
    void* _handlerUser = nullptr;

    uint8_t _readBuf[kReadChunkSize] = {};
    // TX-буферы вынесены в поля класса, чтобы не раздувать стек в sendEnvelope().
    uint8_t _protoTxBuf[ProtoCodec::kMaxEncodedSize] = {};
    uint8_t _frameTxBuf[kMaxPayload + kFrameOverhead] = {};

    // Упаковать payload в frame и отправить через транспорт.
    bool sendFramePayload(const uint8_t* payload, size_t payloadLen);

    // Вызов callback, если он задан.
    void dispatchEnvelope(const Envelope& env) const;

    // Безопасное копирование C-строки с гарантией '\0'.
    static void copyTextSafe(char* dst, size_t dstSize, const char* src);

    // Нормализация batch перед сериализацией.
    static uint8_t clampBatchCount(uint8_t value);
    static void sanitizeBatch(const SetBatch& src, SetBatch& dst);

    // Fallback-правило:
    // если batch не отправился одним Envelope (по любой причине), режем на отдельные сообщения.
    bool sendBatchSplit(const SetBatch& batch);

    // Вспомогательно для split (цвет в batch имеет отдельный тип).
    bool setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor);
};
