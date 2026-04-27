#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "proto/machine.pb.h"
#include "runtime/PageRuntime.h"

// Nanopb генерирует enum names с префиксом типа
// (ElementAttribute_ELEMENT_ATTRIBUTE_*), а ScreenUI generated-код
// использует короткие имена ELEMENT_ATTRIBUTE_* как значения enum.
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_UNKNOWN =
    ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_VISIBLE =
    ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_POSITION_WIDTH =
    ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_POSITION_HEIGHT =
    ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_HEIGHT;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_X =
    ElementAttribute_ELEMENT_ATTRIBUTE_X;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_Y =
    ElementAttribute_ELEMENT_ATTRIBUTE_Y;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_BACKGROUND_COLOR =
    ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_TEXT_COLOR =
    ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_COLOR;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_TEXT_FONT =
    ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_VALUE =
    ElementAttribute_ELEMENT_ATTRIBUTE_VALUE;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_TEXT =
    ElementAttribute_ELEMENT_ATTRIBUTE_TEXT;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_BORDER_COLOR =
    ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_COLOR;
static constexpr ElementAttribute ELEMENT_ATTRIBUTE_BORDER_WIDTH =
    ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH;

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
    ~Signal() { reset(); }

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
        if constexpr (is_std_function<Fn>::value) {
            if (!f) {
                reset();
                return *this;
            }
        }

        reset();
        ::new (static_cast<void*>(_storage)) Fn(std::forward<F>(f));
        _invoke = &invokeImpl<Fn>;
        _destroy = &destroyImpl<Fn>;
        return *this;
    }

    // Сбросить обработчик.
    void reset() {
        if (_destroy != nullptr) {
            _destroy(static_cast<void*>(_storage));
        }
        _invoke = nullptr;
        _destroy = nullptr;
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

    template <typename Fn>
    static void destroyImpl(void* storage) {
        static_cast<Fn*>(storage)->~Fn();
    }

    template <typename>
    struct is_std_function : std::false_type {};

    template <typename R, typename... FnArgs>
    struct is_std_function<std::function<R(FnArgs...)>> : std::true_type {};

    using InvokePtr = void (*)(void*, Args...);
    using DestroyPtr = void (*)(void*);

    alignas(std::max_align_t) unsigned char _storage[kMaxStorage] = {};
    InvokePtr _invoke = nullptr;
    DestroyPtr _destroy = nullptr;
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
    Property(IPage* page, uint32_t elementId) : _page(page), _elementId(elementId) {}

    operator int32_t() const {
        return _page != nullptr ? _page->readIntProperty(_elementId, A) : 0;
    }

    Property& operator=(int32_t v) {
        if (_page != nullptr) {
            _page->writeIntProperty(_elementId, A, v);
        }
        return *this;
    }

private:
    IPage* _page;
    uint32_t _elementId;
};

// --- bool ---
template <ElementAttribute A>
class Property<bool, A> {
public:
    Property(IPage* page, uint32_t elementId) : _page(page), _elementId(elementId) {}

    operator bool() const {
        return _page != nullptr ? _page->readBoolProperty(_elementId, A) : false;
    }

    Property& operator=(bool v) {
        if (_page != nullptr) {
            _page->writeBoolProperty(_elementId, A, v);
        }
        return *this;
    }

private:
    IPage* _page;
    uint32_t _elementId;
};

// --- uint32_t (цвет RGB888) ---
template <ElementAttribute A>
class Property<uint32_t, A> {
public:
    Property(IPage* page, uint32_t elementId) : _page(page), _elementId(elementId) {}

    operator uint32_t() const {
        return _page != nullptr ? _page->readColorProperty(_elementId, A) : 0;
    }

    Property& operator=(uint32_t v) {
        if (_page != nullptr) {
            _page->writeColorProperty(_elementId, A, v);
        }
        return *this;
    }

private:
    IPage* _page;
    uint32_t _elementId;
};

// --- ElementFont ---
template <ElementAttribute A>
class Property<ElementFont, A> {
public:
    Property(IPage* page, uint32_t elementId) : _page(page), _elementId(elementId) {}

    operator ElementFont() const {
        return _page != nullptr
            ? _page->readFontProperty(_elementId, A)
            : ElementFont_ELEMENT_FONT_UNKNOWN;
    }

    Property& operator=(ElementFont v) {
        if (_page != nullptr) {
            _page->writeFontProperty(_elementId, A, v);
        }
        return *this;
    }

private:
    IPage* _page;
    uint32_t _elementId;
};

// --- const char* (строка, копируется в пул модели + отправляется) ---
template <ElementAttribute A>
class Property<const char*, A> {
public:
    Property(IPage* page, uint32_t elementId) : _page(page), _elementId(elementId) {}

    operator const char*() const {
        return _page != nullptr ? _page->readStringProperty(_elementId, A) : nullptr;
    }

    Property& operator=(const char* v) {
        if (_page != nullptr) {
            _page->writeStringProperty(_elementId, A, v);
        }
        return *this;
    }

private:
    IPage* _page;
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
    ElementBase(IPage* page, uint32_t elementId)
      : _page(page), _id(elementId)
      , visible(page, elementId)
      , width  (page, elementId)
      , height (page, elementId) {}

    uint32_t id() const { return _id; }
    IPage* page() { return _page; }
    const IPage* page() const { return _page; }

    Property<bool,    ELEMENT_ATTRIBUTE_VISIBLE>         visible;
    Property<int32_t, ELEMENT_ATTRIBUTE_POSITION_WIDTH>  width;
    Property<int32_t, ELEMENT_ATTRIBUTE_POSITION_HEIGHT> height;

protected:
    IPage* _page;
    uint32_t _id;
};

// ------------------------------------------------------------
// Button — кнопка с текстом, фоном, onClick.
// ------------------------------------------------------------
class Button : public ElementBase {
public:
    Button(IPage* page, uint32_t elementId)
      : ElementBase(page, elementId)
      , text     (page, elementId)
      , bgColor  (page, elementId)
      , textColor(page, elementId) {}

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
    Panel(IPage* page, uint32_t elementId)
      : ElementBase(page, elementId)
      , x      (page, elementId)
      , y      (page, elementId)
      , bgColor(page, elementId) {}

    Property<int32_t,  ELEMENT_ATTRIBUTE_X>                x;
    Property<int32_t,  ELEMENT_ATTRIBUTE_Y>                y;
    Property<uint32_t, ELEMENT_ATTRIBUTE_BACKGROUND_COLOR> bgColor;
};

// ------------------------------------------------------------
// Text — текстовое поле.
// ------------------------------------------------------------
class Text : public ElementBase {
public:
    Text(IPage* page, uint32_t elementId)
      : ElementBase(page, elementId)
      , text     (page, elementId)
      , textColor(page, elementId)
      , font     (page, elementId) {}

    Property<const char*, ELEMENT_ATTRIBUTE_TEXT>       text;
    Property<uint32_t,    ELEMENT_ATTRIBUTE_TEXT_COLOR> textColor;
    Property<ElementFont, ELEMENT_ATTRIBUTE_TEXT_FONT>  font;
};

}  // namespace screenlib
