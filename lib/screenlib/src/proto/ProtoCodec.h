#pragma once

#include <stddef.h>
#include <stdint.h>
#include "machine.pb.h"

// ============================================================
// ProtoCodec — encode/decode Envelope через nanopb.
// Работает с сырыми байтами payload из FrameCodec.
// Не знает ничего про транспорт, framing или UI.
//
// Использование:
//   encode: Envelope → bytes → FrameCodec::pack → ITransport::write
//   decode: ITransport::read → FrameCodec::feed → bytes → Envelope
// ============================================================

class ProtoCodec {
public:
    // Сериализовать Envelope в байты.
    // buf/bufSize — выходной буфер (рекомендуемый размер >= kMaxEncodedSize).
    // Возвращает количество записанных байт или 0 при ошибке.
    // При ошибке текст причины доступен через lastError().
    static size_t encode(const Envelope& env, uint8_t* buf, size_t bufSize);

    // Десериализовать байты в Envelope.
    // Возвращает false при ошибке. Причина — в lastError().
    static bool decode(const uint8_t* data, size_t len, Envelope& out);

    // Строка последней ошибки nanopb (PB_GET_ERROR).
    // Валидна только сразу после неудачного encode/decode.
    // В норме возвращает пустую строку.
    static const char* lastError() { return _lastError; }

    // Рекомендуемый размер буфера для encode.
    // Согласован с kMaxPayload в FrameCodec и лимитами machine.options.
    static constexpr size_t kMaxEncodedSize = 1024;

private:
    // Последняя ошибка — статическая строка, не выделяет память
    static const char* _lastError;
};
