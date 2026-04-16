#pragma once

#include "lvgl_eez/IUiAdapter.h"

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

    bool setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) override {
        (void)elementId;
        (void)bgColor;
        (void)fgColor;
        return false;
    }

    bool applyBatch(const SetBatch& batch) override {
        (void)batch;
        return false;
    }

    void setEventSink(EventSink sink, void* userData) override {
        _sink = sink;
        _sinkUser = userData;
    }

    void tickInput() override {}

private:
    EventSink _sink = nullptr;
    void* _sinkUser = nullptr;
};

}  // namespace screenlib::adapter
