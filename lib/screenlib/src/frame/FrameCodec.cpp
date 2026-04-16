#include "FrameCodec.h"

// ============================================================
// CRC16-CCITT: poly=0x1021, init=0xFFFF, без инверсии
// ============================================================
uint16_t FrameCodec::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc16Update(crc, data[i]);
    }
    return crc;
}

void FrameCodec::crc16Update(uint16_t& crc, uint8_t b) {
    crc ^= static_cast<uint16_t>(b) << 8;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
}

// ============================================================
// Упаковка кадра
// ============================================================
size_t FrameCodec::pack(const uint8_t* payload, uint16_t payloadLen,
                        uint8_t* outBuf, size_t outBufSize,
                        uint8_t seq, uint8_t ver) {
    const size_t frameLen = kFrameOverhead + payloadLen;
    if (payloadLen > kMaxPayload || outBufSize < frameLen) return 0;

    size_t i = 0;
    outBuf[i++] = kFrameSof;
    outBuf[i++] = ver;
    outBuf[i++] = seq;
    outBuf[i++] = static_cast<uint8_t>(payloadLen >> 8);
    outBuf[i++] = static_cast<uint8_t>(payloadLen & 0xFF);
    for (uint16_t j = 0; j < payloadLen; j++) {
        outBuf[i++] = payload[j];
    }

    // CRC считаем от SOF до конца payload
    const uint16_t crc = crc16(outBuf, i);
    outBuf[i++] = static_cast<uint8_t>(crc >> 8);
    outBuf[i++] = static_cast<uint8_t>(crc & 0xFF);

    return frameLen;
}

// ============================================================
// Сброс только состояния парсера (незаконченный кадр)
// ============================================================
void FrameCodec::resetParser() {
    _state      = State::WaitSof;
    _expected   = 0;
    _received   = 0;
    _runningCrc = 0xFFFF;
    _crcHigh    = 0;
    _frame      = Frame{};
}

// ============================================================
// Очистить очередь готовых кадров
// ============================================================
void FrameCodec::clearQueue() {
    _qHead = 0;
    _qTail = 0;
}

// ============================================================
// Обработка буфера: весь data, не останавливается на первом кадре.
// Возвращает количество кадров, добавленных в очередь.
// ============================================================
size_t FrameCodec::feed(const uint8_t* data, size_t len) {
    size_t queued = 0;
    for (size_t i = 0; i < len; i++) {
        if (processByte(data[i])) queued++;
    }
    return queued;
}

// ============================================================
// Извлечь кадр из очереди
// ============================================================
bool FrameCodec::popFrame(Frame& out) {
    if (_qHead == _qTail) return false;
    out    = _queue[_qTail];
    _qTail = (_qTail + 1) % kFrameQueueLen;
    return true;
}

// ============================================================
// Добавить кадр в очередь.
// Возвращает false при переполнении — кадр отброшен,
// значит верхний слой не успевает читать (увеличить kFrameQueueLen).
// ============================================================
bool FrameCodec::queuePush(const Frame& f) {
    if (queueFull()) return false;
    _queue[_qHead] = f;
    _qHead = (_qHead + 1) % kFrameQueueLen;
    return true;
}

// ============================================================
// Конечный автомат: один байт за раз.
// Возвращает true когда кадр добавлен в очередь.
// ============================================================
bool FrameCodec::processByte(uint8_t b) {
    switch (_state) {

        case State::WaitSof:
            if (b == kFrameSof) {
                _runningCrc = 0xFFFF;        // сброс CRC перед новым кадром
                crc16Update(_runningCrc, b); // SOF входит в CRC
                _state = State::WaitVer;
            }
            // Не SOF — ждём дальше (автосинхронизация)
            break;

        case State::WaitVer:
            _frame.ver = b;
            crc16Update(_runningCrc, b);
            _state = State::WaitSeq;
            break;

        case State::WaitSeq:
            _frame.seq = b;
            crc16Update(_runningCrc, b);
            _state = State::WaitLenHi;
            break;

        case State::WaitLenHi:
            _expected = static_cast<uint16_t>(b) << 8;
            crc16Update(_runningCrc, b);
            _state = State::WaitLenLo;
            break;

        case State::WaitLenLo:
            _expected |= b;
            crc16Update(_runningCrc, b);
            if (_expected > kMaxPayload) {
                // Слишком большой payload — рассинхрон, ищем следующий SOF
                _state = State::WaitSof;
                break;
            }
            _received = 0;
            _frame.payloadLen = 0;
            _state = (_expected == 0) ? State::WaitCrcHi : State::ReadPayload;
            break;

        case State::ReadPayload:
            _frame.payload[_received++] = b;
            crc16Update(_runningCrc, b);
            if (_received >= _expected) {
                _frame.payloadLen = _expected;
                _state = State::WaitCrcHi;
            }
            break;

        case State::WaitCrcHi:
            // CRC не входит в CRC — сохраняем отдельно
            _crcHigh = b;
            _state = State::WaitCrcLo;
            break;

        case State::WaitCrcLo: {
            const uint16_t received = (static_cast<uint16_t>(_crcHigh) << 8) | b;
            _state = State::WaitSof;  // готовы к следующему кадру в любом случае
            if (received == _runningCrc) {
                // true только если реально добавили в очередь
                return queuePush(_frame);
            }
            // CRC не совпала — кадр отброшен, продолжаем читать
            break;
        }
    }
    return false;
}
