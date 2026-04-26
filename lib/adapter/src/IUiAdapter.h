#pragma once

#include <stdint.h>

#include "proto/machine.pb.h"

namespace screenlib::adapter {

// Базовый контракт UI-адаптера для клиентской стороны экрана.
// Этот слой не знает про транспорт, только про применение команд к UI
// и публикацию пользовательских событий наверх.
class IUiAdapter {
public:
    // Синк для отправки событий UI -> ScreenClient.
    using EventSink = bool (*)(const Envelope& env, void* userData);

    virtual ~IUiAdapter() = default;

    virtual bool showPage(uint32_t pageId) = 0;
    // Применить один типизированный атрибут элемента (width/height/цвет/шрифт и т.д.).
    virtual bool setElementAttribute(const SetElementAttribute& attr) = 0;
    virtual bool setTextAttribute(uint32_t elementId, const char* text) {
        (void)elementId;
        (void)text;
        return false;
    }

    // Зарегистрировать обработчик исходящих UI-событий.
    virtual void setEventSink(EventSink sink, void* userData) = 0;
    // Вызвать обработку пользовательского ввода в UI runtime.
    virtual void tickInput() = 0;
};

}  // namespace screenlib::adapter
