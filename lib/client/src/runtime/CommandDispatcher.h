#pragma once

#include "lvgl_eez/IUiAdapter.h"

namespace screenlib::client {

// Диспетчер команд экрана: Envelope -> вызовы IUiAdapter.
// Не знает ничего про транспорт, framing и protobuf-кодек.
class CommandDispatcher {
public:
    explicit CommandDispatcher(screenlib::adapter::IUiAdapter& uiAdapter)
        : _uiAdapter(uiAdapter) {}

    // Применить входящую команду к UI.
    // Возвращает true только если это поддержанная экранная команда
    // и она успешно применена адаптером.
    bool dispatch(const Envelope& env);

private:
    screenlib::adapter::IUiAdapter& _uiAdapter;
};

}  // namespace screenlib::client
