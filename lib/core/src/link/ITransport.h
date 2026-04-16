#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
// ITransport — абстракция байтового транспорта.
// Не знает ничего про protobuf, framing, UI или команды.
// Реализации: UartLink (host), WebSocketServerLink (host) и WebSocketClientLink (client).
// ============================================================

class ITransport {
public:
    virtual ~ITransport() = default;

    // Канал инициализирован и готов к обмену.
    // Для UART: true после begin(). Для WS: true когда клиент подключён.
    virtual bool connected() const = 0;

    // Отправить len байт из data.
    // Возвращает true если весь буфер передан транспорту.
    // Поведение зависит от реализации: UART может блокироваться
    // при полном TX-буфере, WebSocket — нет.
    virtual bool write(const uint8_t* data, size_t len) = 0;

    // Неблокирующее чтение из RX-буфера.
    // Возвращает реальное количество прочитанных байт (0..max_len).
    virtual size_t read(uint8_t* dst, size_t max_len) = 0;

    // Вызывать часто в основном loop().
    // Для UART: может быть пустым (или сбрасывать таймауты).
    // Для WebSocket: обработка событий и пополнение RX ring buffer.
    virtual void tick() = 0;
};
