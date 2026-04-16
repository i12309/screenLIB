#include "lvgl_eez/EezLvglAdapter.h"

#include <string.h>

namespace screenlib::adapter {

EezLvglAdapter::EezLvglAdapter(UiObjectMap* objectMap,
                               const EezLvglHooks& hooks,
                               void* hookUserData)
    : _objectMap(objectMap), _hooks(hooks), _hookUserData(hookUserData) {}

void EezLvglAdapter::setObjectMap(UiObjectMap* objectMap) {
    _objectMap = objectMap;
}

void EezLvglAdapter::setHooks(const EezLvglHooks& hooks, void* hookUserData) {
    _hooks = hooks;
    _hookUserData = hookUserData;
}

bool EezLvglAdapter::showPage(uint32_t pageId) {
    if (_objectMap == nullptr || _hooks.showPage == nullptr) {
        return false;
    }

    void* pageTarget = _objectMap->findPage(pageId);
    if (pageTarget == nullptr) {
        return false;
    }

    return _hooks.showPage(_hookUserData, pageTarget);
}

bool EezLvglAdapter::setText(uint32_t elementId, const char* text) {
    if (_objectMap == nullptr || _hooks.setText == nullptr) {
        return false;
    }

    void* uiObject = _objectMap->findElement(elementId);
    if (uiObject == nullptr) {
        return false;
    }

    return _hooks.setText(_hookUserData, uiObject, text != nullptr ? text : "");
}

bool EezLvglAdapter::setValue(uint32_t elementId, int32_t value) {
    if (_objectMap == nullptr || _hooks.setValue == nullptr) {
        return false;
    }

    void* uiObject = _objectMap->findElement(elementId);
    if (uiObject == nullptr) {
        return false;
    }

    return _hooks.setValue(_hookUserData, uiObject, value);
}

bool EezLvglAdapter::setVisible(uint32_t elementId, bool visible) {
    if (_objectMap == nullptr || _hooks.setVisible == nullptr) {
        return false;
    }

    void* uiObject = _objectMap->findElement(elementId);
    if (uiObject == nullptr) {
        return false;
    }

    return _hooks.setVisible(_hookUserData, uiObject, visible);
}

bool EezLvglAdapter::setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) {
    if (_objectMap == nullptr || _hooks.setColor == nullptr) {
        return false;
    }

    void* uiObject = _objectMap->findElement(elementId);
    if (uiObject == nullptr) {
        return false;
    }

    return _hooks.setColor(_hookUserData, uiObject, bgColor, fgColor);
}

bool EezLvglAdapter::applyBatch(const SetBatch& batch) {
    bool allOk = true;

    for (pb_size_t i = 0; i < batch.texts_count; ++i) {
        if (!setText(batch.texts[i].element_id, batch.texts[i].text)) {
            allOk = false;
        }
    }

    for (pb_size_t i = 0; i < batch.values_count; ++i) {
        if (!setValue(batch.values[i].element_id, batch.values[i].value)) {
            allOk = false;
        }
    }

    for (pb_size_t i = 0; i < batch.visibles_count; ++i) {
        if (!setVisible(batch.visibles[i].element_id, batch.visibles[i].visible)) {
            allOk = false;
        }
    }

    for (pb_size_t i = 0; i < batch.colors_count; ++i) {
        if (!setColor(batch.colors[i].element_id,
                      batch.colors[i].bg_color,
                      batch.colors[i].fg_color)) {
            allOk = false;
        }
    }

    return allOk;
}

void EezLvglAdapter::setEventSink(EventSink sink, void* userData) {
    _sink = sink;
    _sinkUser = userData;
}

void EezLvglAdapter::tickInput() {
    if (_hooks.tickInput != nullptr) {
        _hooks.tickInput(_hookUserData, *this);
    }
}

bool EezLvglAdapter::emitButtonEvent(uint32_t elementId, uint32_t pageId) {
    Envelope env{};
    env.which_payload = Envelope_button_event_tag;
    env.payload.button_event.element_id = elementId;
    env.payload.button_event.page_id = pageId;
    return emitEnvelope(env);
}

bool EezLvglAdapter::emitInputEventInt(uint32_t elementId, uint32_t pageId, int32_t value) {
    Envelope env{};
    env.which_payload = Envelope_input_event_tag;
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_int_value_tag;
    env.payload.input_event.value.int_value = value;
    return emitEnvelope(env);
}

bool EezLvglAdapter::emitInputEventString(uint32_t elementId, uint32_t pageId, const char* text) {
    Envelope env{};
    env.which_payload = Envelope_input_event_tag;
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_string_value_tag;
    copyTextSafe(env.payload.input_event.value.string_value,
                 sizeof(env.payload.input_event.value.string_value),
                 text);
    return emitEnvelope(env);
}

bool EezLvglAdapter::emitEnvelope(const Envelope& env) {
    if (_sink == nullptr) {
        return false;
    }
    return _sink(env, _sinkUser);
}

void EezLvglAdapter::copyTextSafe(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

}  // namespace screenlib::adapter
