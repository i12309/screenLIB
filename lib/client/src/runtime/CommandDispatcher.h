#pragma once

#include "IUiAdapter.h"
#include "chunk/TextChunkAssembler.h"

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
    screenlib::chunk::TextChunkAssembler _textAssembler;
};

}  // namespace screenlib::client
