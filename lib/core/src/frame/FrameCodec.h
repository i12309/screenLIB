#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
// FrameCodec — упаковка и разбор кадров поверх байтового потока.
// Не знает ничего про protobuf, UI или транспорт.
//
// Структура кадра:
//   [ SOF | ver | seq | len_hi | len_lo | payload... | crc_hi | crc_lo ]
//     1б    1б    1б     1б       1б       N байт        2 байта
//
// SOF  = kFrameSof  — маркер начала кадра
// ver  = kFrameVer  — версия протокола
// seq  = 0..255     — счётчик кадров (для детекции потерь)
// len  = длина payload (uint16 big-endian)
// crc  = CRC16-CCITT от SOF до конца payload включительно
//
// feed() обрабатывает весь буфер и складывает готовые кадры
// во внутреннюю очередь. Читать через hasFrame() / popFrame().
// После reconnect/disconnect вызывать reset().
// ============================================================

static constexpr uint8_t  kFrameSof     = 0xAB;  // маркер начала кадра
static constexpr uint8_t  kFrameVer     = 1;      // версия протокола
// 1024: SetBatch[8] с текстами по 32 байта занимает ~490 байт.
// 512 было мало — SetBatch превышал лимит при 16 элементах по 64 байта.
static constexpr size_t   kMaxPayload   = 1024;   // макс. байт payload
static constexpr size_t   kFrameOverhead = 7;     // SOF+ver+seq+len(2)+crc(2)
// Ring buffer N-1: при kFrameQueueLen=5 реальная ёмкость = 4 кадра
static constexpr size_t   kFrameQueueLen = 5;

class FrameCodec {
public:
    struct Frame {
        uint8_t  ver        = 0;
        uint8_t  seq        = 0;
        uint8_t  payload[kMaxPayload] = {};
        uint16_t payloadLen = 0;
    };

    // Упаковать payload в кадр.
    // outBuf должен быть не менее (payloadLen + kFrameOverhead).
    // Возвращает итоговый размер кадра или 0 при ошибке.
    static size_t pack(const uint8_t* payload, uint16_t payloadLen,
                       uint8_t* outBuf, size_t outBufSize,
                       uint8_t seq = 0, uint8_t ver = kFrameVer);

    // Обработать весь входной буфер.
    // Возвращает количество кадров, добавленных в очередь за этот вызов.
    // Не останавливается на первом кадре — читает до конца data.
    size_t feed(const uint8_t* data, size_t len);

    // Есть ли хотя бы один готовый кадр в очереди
    bool hasFrame() const { return _qHead != _qTail; }

    // Извлечь следующий кадр из очереди.
    // Возвращает false если очередь пуста.
    bool popFrame(Frame& out);

    // Сбросить состояние парсера (незаконченный кадр).
    // Вызывать после ошибки протокола или смены транспорта.
    void resetParser();

    // Очистить очередь готовых кадров.
    // Вызывать если накопленные кадры уже не актуальны (например, reconnect).
    void clearQueue();

    // Полный сброс: resetParser() + clearQueue().
    // Удобно при reconnect/disconnect транспорта — одним вызовом.
    void reset() { resetParser(); clearQueue(); }

    // CRC16-CCITT: poly=0x1021, init=0xFFFF, без инверсии
    static uint16_t crc16(const uint8_t* data, size_t len);

private:
    enum class State : uint8_t {
        WaitSof,
        WaitVer,
        WaitSeq,
        WaitLenHi,
        WaitLenLo,
        ReadPayload,
        WaitCrcHi,
        WaitCrcLo,
    };

    // --- Состояние парсера ---
    State    _state      = State::WaitSof;
    uint16_t _expected   = 0;       // ожидаемая длина payload
    uint16_t _received   = 0;       // принято байт payload
    uint16_t _runningCrc = 0xFFFF;  // бегущий CRC16 (накапливается побайтово)
    uint8_t  _crcHigh    = 0;       // старший байт принятого CRC
    Frame    _frame;                // собираемый кадр

    // --- Очередь готовых кадров ---
    Frame  _queue[kFrameQueueLen];
    size_t _qHead = 0;
    size_t _qTail = 0;

    bool queueFull()  const { return (_qHead + 1) % kFrameQueueLen == _qTail; }
    // Возвращает false если очередь полна (кадр отброшен)
    bool queuePush(const Frame& f);

    // Обработать один байт. Возвращает true при успешном завершении кадра.
    bool processByte(uint8_t b);

    // Добавить байт в бегущий CRC
    static void crc16Update(uint16_t& crc, uint8_t b);
};
