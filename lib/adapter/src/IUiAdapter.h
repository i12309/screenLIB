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
    virtual bool setText(uint32_t elementId, const char* text) = 0;
    virtual bool setValue(uint32_t elementId, int32_t value) = 0;
    virtual bool setVisible(uint32_t elementId, bool visible) = 0;
    virtual bool setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) = 0;
    virtual bool applyBatch(const SetBatch& batch) = 0;

    // Зарегистрировать обработчик исходящих UI-событий.
    virtual void setEventSink(EventSink sink, void* userData) = 0;
    // Вызвать обработку пользовательского ввода в UI runtime.
    virtual void tickInput() = 0;
};

}  // namespace screenlib::adapter
