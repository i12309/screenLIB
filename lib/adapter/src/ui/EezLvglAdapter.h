#pragma once

#include "ui/IUiAdapter.h"

namespace screenlib::adapter {

// Заготовка адаптера EEZ/LVGL.
// Реализация будет связывать element_id с конкретными LVGL объектами.
class EezLvglAdapter : public IUiAdapter {
public:
    bool showPage(uint32_t pageId) override {
        (void)pageId;
        return false;
    }

    bool setText(uint32_t elementId, const char* text) override {
        (void)elementId;
        (void)text;
        return false;
    }

    bool setValue(uint32_t elementId, int32_t value) override {
        (void)elementId;
        (void)value;
        return false;
    }

    bool setVisible(uint32_t elementId, bool visible) override {
        (void)elementId;
        (void)visible;
        return false;
    }
};

}  // namespace screenlib::adapter
