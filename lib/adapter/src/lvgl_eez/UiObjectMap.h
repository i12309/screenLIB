#pragma once

#include <stddef.h>
#include <stdint.h>

namespace screenlib::adapter {

// Binding для element_id -> lv_obj_t* (или другой UI object pointer).
struct UiObjectBinding {
    uint32_t elementId = 0;
    void* uiObject = nullptr;
};

// Binding для page_id -> screen/switch target.
struct UiPageBinding {
    uint32_t pageId = 0;
    void* pageTarget = nullptr;
};

// Хранилище mapping без transport/proto логики.
// Память под bindings передается снаружи, чтобы избежать динамики в runtime.
class UiObjectMap {
public:
    UiObjectMap(UiObjectBinding* objectStorage,
                size_t objectCapacity,
                UiPageBinding* pageStorage = nullptr,
                size_t pageCapacity = 0);

    bool bindElement(uint32_t elementId, void* uiObject);
    bool bindPage(uint32_t pageId, void* pageTarget);

    void* findElement(uint32_t elementId) const;
    void* findPage(uint32_t pageId) const;

    void clear();

    size_t elementCount() const { return _objectCount; }
    size_t pageCount() const { return _pageCount; }

private:
    UiObjectBinding* _objectStorage = nullptr;
    size_t _objectCapacity = 0;
    size_t _objectCount = 0;

    UiPageBinding* _pageStorage = nullptr;
    size_t _pageCapacity = 0;
    size_t _pageCount = 0;
};

}  // namespace screenlib::adapter
