#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "proto/machine.pb.h"
#include "runtime/PageRuntime.h"

namespace screenlib {

// ============================================================
// Signal — fixed-storage делегат без heap-аллокаций.
//
// Аналог std::function<void(Args...)>, но:
//   - хранит callable во внутреннем буфере (kMaxStorage байт);
//   - НЕ делает heap-аллокаций, никогда;
//   - принимает только trivially-destructible callables
//     (лямбды с тривиальным захватом: value-type, указатели, &);
//   - если callable не помещается в storage — ошибка компиляции
//     через static_assert, пользователь видит проблему на месте.
//
// Типичное использование (на стороне страницы бэка):
//
//   btn_MAIN_OK.onClick = [this]{ this->handleOk(); };
//
// Захват [this] = 8 байт + указатель на вызов = 8 байт. Легко укладывается.
//
// Выбор 32 байт для kMaxStorage — эмпирический компромисс:
// хватает на захват [this, pageId, elementId, ...] и не раздувает
// размер объекта страницы при десятках сигналов.
// ============================================================
template <typename... Args>
class Signal {
public:
    static constexpr std::size_t kMaxStorage = 32;

    Signal() = default;

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    // Присваивание callable. Копирует его во внутренний storage.
    template <typename F,
              typename = std::enable_if_t<
                  !std::is_same_v<std::decay_t<F>, Signal>>>
    Signal& operator=(F&& f) {
        using Fn = std::decay_t<F>;
        static_assert(sizeof(Fn) <= kMaxStorage,
                      "Signal capture is too large; enlarge kMaxStorage or reduce capture");
        static_assert(alignof(Fn) <= alignof(std::max_align_t),
                      "Signal capture alignment is unsupported");
        static_assert(std::is_trivially_destructible_v<Fn>,
                      "Signal callable must be trivially destructible (no std::string etc. in capture)");

        ::new (static_cast<void*>(_storage)) Fn(std::forward<F>(f));
        _invoke = &invokeImpl<Fn>;
        return *this;
    }

    // Сбросить обработчик.
    void reset() {
        _invoke = nullptr;
    }

    // Установлен ли обработчик.
    explicit operator bool() const { return _invoke != nullptr; }

    // Вызвать обработчик (если задан).
    void emit(Args... args) const {
        if (_invoke != nullptr) {
            _invoke(const_cast<void*>(static_cast<const void*>(_storage)),
                    std::forward<Args>(args)...);
        }
    }

private:
    template <typename Fn>
    static void invokeImpl(void* storage, Args... args) {
        (*static_cast<Fn*>(storage))(std::forward<Args>(args)...);
    }

    using InvokePtr = void (*)(void*, Args...);

    alignas(std::max_align_t) unsigned char _storage[kMaxStorage] = {};
    InvokePtr _invoke = nullptr;
};

// ============================================================
// Property<T, A> — типизированное свойство одного атрибута.
//
// Чтение (operator T()) — всегда из локальной PageModel.
// Запись (operator=) — обновляет PageModel локально (оптимизм)
//                       и отправляет SetElementAttribute через runtime.
//
// Специализируется по типу T и атрибуту A.
// Реализации для int32_t, bool, uint32_t (color), ElementFont, const char*
// — см. ниже.
// ============================================================
template <typename T, ElementAttribute A>
class Property;

// --- int32_t ---
template <ElementAttribute A>
class Property<int32_t, A> {
public:
    Property(PageRuntime* rt, uint32_t elementId) : _rt(rt), _elementId(elementId) {}

    operator int32_t() const {
        return _rt->model().getInt(_elementId, A);
    }

    Property& operator=(int32_t v) {
        _rt->model().setInt(_elementId, A, v);
        ElementAttributeValue eav{};
        eav.attribute = A;
        eav.which_value = ElementAttributeValue_int_value_tag;
        eav.value.int_value = v;
        _rt->sendSetAttribute(_elementId, eav);
        return *this;
    }

private:
    PageRuntime* _rt;
    uint32_t _elementId;
};

// --- bool ---
template <ElementAttribute A>
class Property<bool, A> {
public:
    Property(PageRuntime* rt, uint32_t elementId) : _rt(rt), _elementId(elementId) {}

    operator bool() const {
        return _rt->model().getBool(_elementId, A);
    }

    Property& operator=(bool v) {
        _rt->model().setBool(_elementId, A, v);
        ElementAttributeValue eav{};
        eav.attribute = A;
        eav.which_value = ElementAttributeValue_bool_value_tag;
        eav.value.bool_value = v;
        _rt->sendSetAttribute(_elementId, eav);
        return *this;
    }

private:
    PageRuntime* _rt;
    uint32_t _elementId;
};

// --- uint32_t (цвет RGB888) ---
template <ElementAttribute A>
class Property<uint32_t, A> {
public:
    Property(PageRuntime* rt, uint32_t elementId) : _rt(rt), _elementId(elementId) {}

    operator uint32_t() const {
        return _rt->model().getColor(_elementId, A);
    }

    Property& operator=(uint32_t v) {
        _rt->model().setColor(_elementId, A, v);
        ElementAttributeValue eav{};
        eav.attribute = A;
        eav.which_value = ElementAttributeValue_color_value_tag;
        eav.value.color_value = v & 0x00FFFFFFu;
        _rt->sendSetAttribute(_elementId, eav);
        return *this;
    }

private:
    PageRuntime* _rt;
    uint32_t _elementId;
};

// --- ElementFont ---
template <ElementAttribute A>
class Property<ElementFont, A> {
public:
    Property(PageRuntime* rt, uint32_t elementId) : _rt(rt), _elementId(elementId) {}

    operator ElementFont() const {
        return _rt->model().getFont(_elementId, A);
    }

    Property& operator=(ElementFont v) {
        _rt->model().setFont(_elementId, A, v);
        ElementAttributeValue eav{};
        eav.attribute = A;
        eav.which_value = ElementAttributeValue_font_value_tag;
        eav.value.font_value = v;
        _rt->sendSetAttribute(_elementId, eav);
        return *this;
    }

private:
    PageRuntime* _rt;
    uint32_t _elementId;
};

// --- const char* (строка, копируется в пул модели + отправляется) ---
template <ElementAttribute A>
class Property<const char*, A> {
public:
    Property(PageRuntime* rt, uint32_t elementId) : _rt(rt), _elementId(elementId) {}

    operator const char*() const {
        return _rt->model().getString(_elementId, A);
    }

    Property& operator=(const char* v) {
        _rt->model().setString(_elementId, A, v);
        ElementAttributeValue eav{};
        eav.attribute = A;
        eav.which_value = ElementAttributeValue_string_value_tag;
        // Защита от nullptr + обрезка по размеру поля nanopb.
        const char* src = (v != nullptr) ? v : "";
        std::strncpy(eav.value.string_value, src, sizeof(eav.value.string_value) - 1);
        eav.value.string_value[sizeof(eav.value.string_value) - 1] = '\0';
        _rt->sendSetAttribute(_elementId, eav);
        return *this;
    }

private:
    PageRuntime* _rt;
    uint32_t _elementId;
};

// ============================================================
// ElementBase — общий набор свойств, доступный у любого элемента.
// Конкретные типы (Button/Panel/Text/...) наследуют его и добавляют
// собственные property + сигналы.
//
// Типы конкретных элементов генерируются в ScreenUI
// (см. element_types.generated.h). Здесь — базовый класс и парочка
// "эталонных" типов, которые нужны до появления генератора.
// ============================================================
class ElementBase {
public:
    ElementBase(PageRuntime* rt, uint32_t elementId)
      : _rt(rt), _id(elementId)
      , visible(rt, elementId)
      , width  (rt, elementId)
      , height (rt, elementId) {}

    uint32_t id() const { return _id; }
    PageRuntime* runtime() { return _rt; }

    Property<bool,    ELEMENT_ATTRIBUTE_VISIBLE>         visible;
    Property<int32_t, ELEMENT_ATTRIBUTE_POSITION_WIDTH>  width;
    Property<int32_t, ELEMENT_ATTRIBUTE_POSITION_HEIGHT> height;

protected:
    PageRuntime* _rt;
    uint32_t _id;
};

// ------------------------------------------------------------
// Button — кнопка с текстом, фоном, onClick.
// ------------------------------------------------------------
class Button : public ElementBase {
public:
    Button(PageRuntime* rt, uint32_t elementId)
      : ElementBase(rt, elementId)
      , text     (rt, elementId)
      , bgColor  (rt, elementId)
      , textColor(rt, elementId) {}

    Property<const char*, ELEMENT_ATTRIBUTE_TEXT>             text;
    Property<uint32_t,    ELEMENT_ATTRIBUTE_BACKGROUND_COLOR> bgColor;
    Property<uint32_t,    ELEMENT_ATTRIBUTE_TEXT_COLOR>       textColor;

    Signal<> onClick;
};

// ------------------------------------------------------------
// Panel — контейнер с позицией.
// ------------------------------------------------------------
class Panel : public ElementBase {
public:
    Panel(PageRuntime* rt, uint32_t elementId)
      : ElementBase(rt, elementId)
      , x      (rt, elementId)
      , y      (rt, elementId)
      , bgColor(rt, elementId) {}

    Property<int32_t,  ELEMENT_ATTRIBUTE_X>                x;
    Property<int32_t,  ELEMENT_ATTRIBUTE_Y>                y;
    Property<uint32_t, ELEMENT_ATTRIBUTE_BACKGROUND_COLOR> bgColor;
};

// ------------------------------------------------------------
// Text — текстовое поле.
// ------------------------------------------------------------
class Text : public ElementBase {
public:
    Text(PageRuntime* rt, uint32_t elementId)
      : ElementBase(rt, elementId)
      , text     (rt, elementId)
      , textColor(rt, elementId)
      , font     (rt, elementId) {}

    Property<const char*, ELEMENT_ATTRIBUTE_TEXT>       text;
    Property<uint32_t,    ELEMENT_ATTRIBUTE_TEXT_COLOR> textColor;
    Property<ElementFont, ELEMENT_ATTRIBUTE_TEXT_FONT>  font;
};

}  // namespace screenlib
