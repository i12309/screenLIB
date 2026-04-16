#include "lvgl_eez/UiObjectMap.h"

namespace screenlib::adapter {

UiObjectMap::UiObjectMap(UiObjectBinding* objectStorage,
                         size_t objectCapacity,
                         UiPageBinding* pageStorage,
                         size_t pageCapacity)
    : _objectStorage(objectStorage),
      _objectCapacity(objectCapacity),
      _pageStorage(pageStorage),
      _pageCapacity(pageCapacity) {}

bool UiObjectMap::bindElement(uint32_t elementId, void* uiObject) {
    if (_objectStorage == nullptr || _objectCapacity == 0) {
        return false;
    }

    for (size_t i = 0; i < _objectCount; ++i) {
        if (_objectStorage[i].elementId == elementId) {
            _objectStorage[i].uiObject = uiObject;
            return true;
        }
    }

    if (_objectCount >= _objectCapacity) {
        return false;
    }

    _objectStorage[_objectCount].elementId = elementId;
    _objectStorage[_objectCount].uiObject = uiObject;
    _objectCount++;
    return true;
}

bool UiObjectMap::bindPage(uint32_t pageId, void* pageTarget) {
    if (_pageStorage == nullptr || _pageCapacity == 0) {
        return false;
    }

    for (size_t i = 0; i < _pageCount; ++i) {
        if (_pageStorage[i].pageId == pageId) {
            _pageStorage[i].pageTarget = pageTarget;
            return true;
        }
    }

    if (_pageCount >= _pageCapacity) {
        return false;
    }

    _pageStorage[_pageCount].pageId = pageId;
    _pageStorage[_pageCount].pageTarget = pageTarget;
    _pageCount++;
    return true;
}

void* UiObjectMap::findElement(uint32_t elementId) const {
    if (_objectStorage == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < _objectCount; ++i) {
        if (_objectStorage[i].elementId == elementId) {
            return _objectStorage[i].uiObject;
        }
    }

    return nullptr;
}

void* UiObjectMap::findPage(uint32_t pageId) const {
    if (_pageStorage == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < _pageCount; ++i) {
        if (_pageStorage[i].pageId == pageId) {
            return _pageStorage[i].pageTarget;
        }
    }

    return nullptr;
}

void UiObjectMap::clear() {
    _objectCount = 0;
    _pageCount = 0;
}

}  // namespace screenlib::adapter
