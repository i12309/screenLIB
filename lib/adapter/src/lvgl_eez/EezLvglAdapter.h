#pragma once

#include "IUiAdapter.h"
#include "lvgl_eez/UiObjectMap.h"

namespace screenlib::adapter {

class EezLvglAdapter;

// Hooks для интеграции с реальным EEZ/LVGL generated UI.
// Библиотека не дублирует generated код, а вызывает переданные callbacks.
struct EezLvglHooks {
    bool (*showPage)(void* userData, void* pageTarget) = nullptr;
    bool (*setText)(void* userData, void* uiObject, const char* text) = nullptr;
    bool (*setValue)(void* userData, void* uiObject, int32_t value) = nullptr;
    bool (*setVisible)(void* userData, void* uiObject, bool visible) = nullptr;
    bool (*setColor)(void* userData, void* uiObject, uint32_t bgColor, uint32_t fgColor) = nullptr;
    // Включает fallback-хелперы для стандартных LVGL объектов, если hook отсутствует или вернул false.
    bool enableLvglObjectHelpers = true;

    // Опциональный poll/flush входных событий UI.
    // Может быть no-op, если UI callbacks публикуют события сразу.
    void (*tickInput)(void* userData, EezLvglAdapter& adapter) = nullptr;
};

// Первая concrete реализация IUiAdapter для EEZ/LVGL.
class EezLvglAdapter : public IUiAdapter {
public:
    EezLvglAdapter(UiObjectMap* objectMap = nullptr,
                   const EezLvglHooks& hooks = EezLvglHooks{},
                   void* hookUserData = nullptr);

    void setObjectMap(UiObjectMap* objectMap);
    void setHooks(const EezLvglHooks& hooks, void* hookUserData = nullptr);

    bool showPage(uint32_t pageId) override;
    bool setText(uint32_t elementId, const char* text) override;
    bool setValue(uint32_t elementId, int32_t value) override;
    bool setVisible(uint32_t elementId, bool visible) override;
    bool setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) override;
    bool applyBatch(const SetBatch& batch) override;

    void setEventSink(EventSink sink, void* userData) override;
    void tickInput() override;

    // Методы для UI callback-ов (LVGL/EEZ -> protocol events).
    bool emitButtonEvent(uint32_t elementId, uint32_t pageId);
    bool emitInputEventInt(uint32_t elementId, uint32_t pageId, int32_t value);
    bool emitInputEventString(uint32_t elementId, uint32_t pageId, const char* text);

private:
    UiObjectMap* _objectMap = nullptr;
    EezLvglHooks _hooks{};
    void* _hookUserData = nullptr;

    EventSink _sink = nullptr;
    void* _sinkUser = nullptr;

    bool emitEnvelope(const Envelope& env);
    static void copyTextSafe(char* dst, size_t dstSize, const char* src);
};

}  // namespace screenlib::adapter
