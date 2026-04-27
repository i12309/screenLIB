#include "PageModel.h"

#include <cstring>

#include "log/ScreenLibLogger.h"

namespace screenlib {

namespace {

constexpr const char* kLogTag = "screenlib.model";

}  // namespace

void PageModel::beginPage(uint32_t pageId, uint32_t sessionId) {
    _pageId = pageId;
    _sessionId = sessionId;
    _ready = false;
    resetStorage();
}

void PageModel::clear() {
    _pageId = 0;
    _sessionId = 0;
    _ready = false;
    resetStorage();
}

void PageModel::resetStorage() {
    _slotCount = 0;
    _stringPoolUsed = 0;
    // Сам массив _slots обнулять не нужно — доступ идёт только в диапазоне [0, _slotCount).
}

void PageModel::applySnapshot(const PageSnapshot& snap) {
    if (snap.page_id != _pageId) {
        SCREENLIB_LOGW(kLogTag,
                       "snapshot page mismatch: expected=%u got=%u",
                       static_cast<unsigned>(_pageId),
                       static_cast<unsigned>(snap.page_id));
    }
    if (snap.session_id != _sessionId) {
        SCREENLIB_LOGW(kLogTag,
                       "snapshot session mismatch: expected=%u got=%u page=%u",
                       static_cast<unsigned>(_sessionId),
                       static_cast<unsigned>(snap.session_id),
                       static_cast<unsigned>(snap.page_id));
    }

    // Полный слепок заменяет локальное состояние.
    resetStorage();

    const std::size_t elemCount = snap.elements_count;
    for (std::size_t i = 0; i < elemCount; ++i) {
        const ElementSnapshot& es = snap.elements[i];
        const std::size_t attrCount = es.attributes_count;
        for (std::size_t j = 0; j < attrCount; ++j) {
            applyValue(es.element_id, es.attributes[j]);
        }
    }
}

void PageModel::applyRemoteChange(const AttributeChanged& msg) {
    if (msg.page_id != _pageId || msg.session_id != _sessionId) {
        SCREENLIB_LOGD(kLogTag,
                       "stale AttributeChanged ignored: page=%u/%u session=%u/%u element=%u",
                       static_cast<unsigned>(msg.page_id),
                       static_cast<unsigned>(_pageId),
                       static_cast<unsigned>(msg.session_id),
                       static_cast<unsigned>(_sessionId),
                       static_cast<unsigned>(msg.element_id));
        return;
    }
    if (!msg.has_value) {
        SCREENLIB_LOGW(kLogTag,
                       "AttributeChanged without value element=%u",
                       static_cast<unsigned>(msg.element_id));
        return;
    }
    applyValue(msg.element_id, msg.value);
}

void PageModel::applyValue(uint32_t elementId, const ElementAttributeValue& v) {
    Slot* slot = insertSlot(elementId, v.attribute);
    if (slot == nullptr) {
        SCREENLIB_LOGW(kLogTag,
                       "page model full, drop element=%u attr=%u",
                       static_cast<unsigned>(elementId),
                       static_cast<unsigned>(v.attribute));
        return;
    }

    AttributeValue& av = slot->value;
    switch (v.which_value) {
        case ElementAttributeValue_int_value_tag:
            av.type = AttributeValue::Type::Int;
            av.i = v.value.int_value;
            break;
        case ElementAttributeValue_color_value_tag:
            av.type = AttributeValue::Type::Color;
            av.u = v.value.color_value & 0x00FFFFFFu;
            break;
        case ElementAttributeValue_font_value_tag:
            av.type = AttributeValue::Type::Font;
            av.font = v.value.font_value;
            break;
        case ElementAttributeValue_bool_value_tag:
            av.type = AttributeValue::Type::Bool;
            av.b = v.value.bool_value;
            break;
        case ElementAttributeValue_string_value_tag:
            av.type = AttributeValue::Type::String;
            av.s = internString(v.value.string_value);
            break;
        default:
            SCREENLIB_LOGW(kLogTag,
                           "unknown ElementAttributeValue tag=%u element=%u attr=%u",
                           static_cast<unsigned>(v.which_value),
                           static_cast<unsigned>(elementId),
                           static_cast<unsigned>(v.attribute));
            av.type = AttributeValue::Type::None;
            break;
    }
}

// ---------- Read ----------

int32_t PageModel::getInt(uint32_t elementId, ElementAttribute a) const {
    const Slot* s = findSlot(elementId, a);
    return (s != nullptr && s->value.type == AttributeValue::Type::Int) ? s->value.i : 0;
}

bool PageModel::getBool(uint32_t elementId, ElementAttribute a) const {
    const Slot* s = findSlot(elementId, a);
    return (s != nullptr && s->value.type == AttributeValue::Type::Bool) ? s->value.b : false;
}

uint32_t PageModel::getColor(uint32_t elementId, ElementAttribute a) const {
    const Slot* s = findSlot(elementId, a);
    return (s != nullptr && s->value.type == AttributeValue::Type::Color) ? s->value.u : 0;
}

ElementFont PageModel::getFont(uint32_t elementId, ElementAttribute a) const {
    const Slot* s = findSlot(elementId, a);
    return (s != nullptr && s->value.type == AttributeValue::Type::Font)
        ? s->value.font
        : ElementFont_ELEMENT_FONT_UNKNOWN;
}

const char* PageModel::getString(uint32_t elementId, ElementAttribute a) const {
    const Slot* s = findSlot(elementId, a);
    return (s != nullptr && s->value.type == AttributeValue::Type::String) ? s->value.s : nullptr;
}

bool PageModel::has(uint32_t elementId, ElementAttribute a) const {
    return findSlot(elementId, a) != nullptr;
}

// ---------- Write ----------

void PageModel::setInt(uint32_t elementId, ElementAttribute a, int32_t v) {
    Slot* slot = insertSlot(elementId, a);
    if (slot == nullptr) return;
    slot->value.type = AttributeValue::Type::Int;
    slot->value.i = v;
}

void PageModel::setBool(uint32_t elementId, ElementAttribute a, bool v) {
    Slot* slot = insertSlot(elementId, a);
    if (slot == nullptr) return;
    slot->value.type = AttributeValue::Type::Bool;
    slot->value.b = v;
}

void PageModel::setColor(uint32_t elementId, ElementAttribute a, uint32_t rgb888) {
    Slot* slot = insertSlot(elementId, a);
    if (slot == nullptr) return;
    slot->value.type = AttributeValue::Type::Color;
    slot->value.u = rgb888 & 0x00FFFFFFu;
}

void PageModel::setFont(uint32_t elementId, ElementAttribute a, ElementFont v) {
    Slot* slot = insertSlot(elementId, a);
    if (slot == nullptr) return;
    slot->value.type = AttributeValue::Type::Font;
    slot->value.font = v;
}

void PageModel::setString(uint32_t elementId, ElementAttribute a, const char* v) {
    Slot* slot = insertSlot(elementId, a);
    if (slot == nullptr) return;
    slot->value.type = AttributeValue::Type::String;
    slot->value.s = internString(v);
}

// ---------- Internal ----------

PageModel::Slot* PageModel::findSlot(uint32_t elementId, ElementAttribute a) {
    for (std::size_t i = 0; i < _slotCount; ++i) {
        if (_slots[i].elementId == elementId && _slots[i].attribute == a) {
            return &_slots[i];
        }
    }
    return nullptr;
}

const PageModel::Slot* PageModel::findSlot(uint32_t elementId, ElementAttribute a) const {
    for (std::size_t i = 0; i < _slotCount; ++i) {
        if (_slots[i].elementId == elementId && _slots[i].attribute == a) {
            return &_slots[i];
        }
    }
    return nullptr;
}

PageModel::Slot* PageModel::insertSlot(uint32_t elementId, ElementAttribute a) {
    Slot* existing = findSlot(elementId, a);
    if (existing != nullptr) {
        return existing;
    }
    if (_slotCount >= kMaxSlots) {
        return nullptr;
    }
    Slot& s = _slots[_slotCount++];
    s.elementId = elementId;
    s.attribute = a;
    s.value = AttributeValue{};
    return &s;
}

const char* PageModel::internString(const char* s) {
    if (s == nullptr) return nullptr;

    const std::size_t len = std::strlen(s);
    // Нужно len + 1 байт (с нулевым терминатором).
    if (_stringPoolUsed + len + 1 > kStringPoolSize) {
        // Обрезка: сколько поместится, плюс место под '\0'.
        if (_stringPoolUsed + 1 > kStringPoolSize) {
            // Пул забит вплотную, нет места даже на пустую строку.
            SCREENLIB_LOGW(kLogTag, "string pool exhausted, drop len=%u",
                           static_cast<unsigned>(len));
            return nullptr;
        }
        const std::size_t avail = kStringPoolSize - _stringPoolUsed - 1;
        char* dst = &_stringPool[_stringPoolUsed];
        std::memcpy(dst, s, avail);
        dst[avail] = '\0';
        _stringPoolUsed += avail + 1;
        SCREENLIB_LOGW(kLogTag, "string pool truncation: %u -> %u",
                       static_cast<unsigned>(len),
                       static_cast<unsigned>(avail));
        return dst;
    }

    char* dst = &_stringPool[_stringPoolUsed];
    std::memcpy(dst, s, len);
    dst[len] = '\0';
    _stringPoolUsed += len + 1;
    return dst;
}

}  // namespace screenlib
