#pragma once

#include <stddef.h>
#include <stdint.h>

namespace screenlib::adapter {

// Заготовка для mapping element_id -> UI object pointer.
// Конкретный тип uiObject зависит от выбранного UI runtime (LVGL/EEZ и т.д.).
struct UiObjectBinding {
    uint32_t elementId = 0;
    void* uiObject = nullptr;
};

class UiObjectMap {
public:
    UiObjectMap(UiObjectBinding* storage, size_t capacity)
        : _storage(storage), _capacity(capacity) {}

    bool bind(uint32_t elementId, void* uiObject) {
        if (_storage == nullptr || _capacity == 0) return false;

        for (size_t i = 0; i < _count; ++i) {
            if (_storage[i].elementId == elementId) {
                _storage[i].uiObject = uiObject;
                return true;
            }
        }

        if (_count >= _capacity) return false;
        _storage[_count].elementId = elementId;
        _storage[_count].uiObject = uiObject;
        _count++;
        return true;
    }

    void* find(uint32_t elementId) const {
        if (_storage == nullptr) return nullptr;
        for (size_t i = 0; i < _count; ++i) {
            if (_storage[i].elementId == elementId) {
                return _storage[i].uiObject;
            }
        }
        return nullptr;
    }

private:
    UiObjectBinding* _storage = nullptr;
    size_t _capacity = 0;
    size_t _count = 0;
};

}  // namespace screenlib::adapter
