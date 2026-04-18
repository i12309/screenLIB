#pragma once

#include <stdint.h>

#include "proto/machine.pb.h"
#include "types/ScreenTypes.h"

namespace screenlib {

class SinglePageRuntime;

// Тонкий прокси на элемент текущей страницы. Делегирует в ScreenSystem
// через runtime. Безопасно по значению, ничего не аллоцирует.
class Element {
public:
    Element(SinglePageRuntime* rt, uint32_t id) : _rt(rt), _id(id) {}

    bool setText(const char* text);
    bool setValue(int32_t value);
    bool setVisible(bool visible);

    uint32_t id() const { return _id; }

private:
    SinglePageRuntime* _rt;
    uint32_t _id;
};

// Базовый интерфейс страницы для модели "одна живая страница".
// Конкретные страницы наследуются от сгенерированных в screenUI base-классов,
// которые в свою очередь наследуются от IHostPage и зашивают pageId().
class IHostPage {
    friend class SinglePageRuntime;

public:
    virtual ~IHostPage() = default;
    virtual uint32_t pageId() const = 0;

protected:
    // Жизненный цикл страницы. Вызываются runtime'ом при navigate<T>().
    virtual void onShow() {}
    virtual void onClose() {}

    // Точки входа для событий с экрана. Сгенерированный base обычно
    // переопределяет onButton/onInput и диспатчит в типизированные хендлеры
    // конкретных элементов (onClickXxx и т.п.).
    virtual void onButton(uint32_t elementId) { (void)elementId; }
    virtual void onInputInt(uint32_t elementId, int32_t value) {
        (void)elementId; (void)value;
    }
    virtual void onInputText(uint32_t elementId, const char* text) {
        (void)elementId; (void)text;
    }

    // Хелперы для бизнес-логики.
    Element element(uint32_t elementId) { return Element(_runtime, elementId); }

    template <typename T>
    void navigate();

    SinglePageRuntime* runtime() { return _runtime; }

private:
    SinglePageRuntime* _runtime = nullptr;
};

}  // namespace screenlib
