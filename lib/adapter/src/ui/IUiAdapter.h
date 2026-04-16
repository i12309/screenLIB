#pragma once

#include <stdint.h>

namespace screenlib::adapter {

// Базовый UI-адаптер: отображение message-level команд на конкретный UI runtime.
class IUiAdapter {
public:
    virtual ~IUiAdapter() = default;

    virtual bool showPage(uint32_t pageId) = 0;
    virtual bool setText(uint32_t elementId, const char* text) = 0;
    virtual bool setValue(uint32_t elementId, int32_t value) = 0;
    virtual bool setVisible(uint32_t elementId, bool visible) = 0;
};

}  // namespace screenlib::adapter
