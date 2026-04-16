#include "ProtoCodec.h"
#include <pb_encode.h>
#include <pb_decode.h>

// Определение статического члена
const char* ProtoCodec::_lastError = "";

// ============================================================
// Encode: Envelope → сырые байты для FrameCodec
// ============================================================
size_t ProtoCodec::encode(const Envelope& env, uint8_t* buf, size_t bufSize) {
    pb_ostream_t stream = pb_ostream_from_buffer(buf, bufSize);

    if (!pb_encode(&stream, Envelope_fields, &env)) {
        // PB_GET_ERROR возвращает const char* с описанием причины
        _lastError = PB_GET_ERROR(&stream);
        return 0;
    }

    _lastError = "";
    return stream.bytes_written;
}

// ============================================================
// Decode: сырые байты из FrameCodec → Envelope
// ============================================================
bool ProtoCodec::decode(const uint8_t* data, size_t len, Envelope& out) {
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    // Обнуляем структуру перед декодом — nanopb не трогает незаданные поля
    out = Envelope{};

    if (!pb_decode(&stream, Envelope_fields, &out)) {
        _lastError = PB_GET_ERROR(&stream);
        return false;
    }

    _lastError = "";
    return true;
}
