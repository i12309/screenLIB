#pragma once

#include <stdint.h>

#include <memory>
#include <type_traits>

#include "config/ScreenConfig.h"
#include "pages/IHostPage.h"
#include "system/ScreenSystem.h"

namespace screenlib {

// SinglePageRuntime — модель "одна живая страница".
// В каждый момент времени существует ровно один экземпляр IHostPage.
// Переход выполняется через navigate<T>(): старая onClose, удаляется,
// создаётся новая, onShow. Никакой регистрации страниц не требуется.
class SinglePageRuntime {
public:
    using EventObserver = void (*)(const Envelope& env, const ScreenEventContext& ctx, void* userData);

    bool init(const ScreenConfig& cfg);
    void tick();
    void setEventObserver(EventObserver observer, void* userData = nullptr) {
        _eventObserver = observer;
        _eventObserverUser = userData;
    }

    // Стартовая страница. Эквивалентно navigate<T>().
    template <typename T>
    bool start() {
        return navigateTo<T>();
    }

    // Переход на страницу типа T. Тип T должен наследоваться от IHostPage.
    template <typename T>
    bool navigateTo() {
        static_assert(std::is_base_of<IHostPage, T>::value,
                      "navigate target must inherit from screenlib::IHostPage");
        std::unique_ptr<IHostPage> next(new T());
        return swapCurrent(std::move(next));
    }

    // Текущая страница (может быть nullptr, если runtime в "тихом" режиме).
    IHostPage* current() const { return _current.get(); }
    uint32_t currentPageId() const { return _current ? _current->pageId() : 0; }

    // Доступ к нижележащему ScreenSystem для адресных команд и для BOOT.
    ScreenSystem& screens() { return _screens; }
    const ScreenSystem& screens() const { return _screens; }

    bool connectedPhysical() const { return _screens.connectedPhysical(); }
    bool connectedWeb() const { return _screens.connectedWeb(); }
    const char* lastError() const { return _screens.lastError(); }

    // Команды на текущий экран — используются Element и пользовательским кодом.
    bool setText(uint32_t elementId, const char* text)   { return _screens.setText(elementId, text); }
    bool setValue(uint32_t elementId, int32_t value)     { return _screens.setValue(elementId, value); }
    bool setVisible(uint32_t elementId, bool visible)    { return _screens.setVisible(elementId, visible); }

private:
    static void onScreenEvent(const Envelope& env, const ScreenEventContext& ctx, void* userData);
    void dispatch(const Envelope& env, const ScreenEventContext& ctx);
    bool swapCurrent(std::unique_ptr<IHostPage> next);

    ScreenSystem _screens;
    std::unique_ptr<IHostPage> _current;
    bool _initialized = false;
    EventObserver _eventObserver = nullptr;
    void* _eventObserverUser = nullptr;
};

// Реализация навигации из страницы — определяется здесь, чтобы template
// видел определение SinglePageRuntime.
template <typename T>
inline void IHostPage::navigate() {
    if (_runtime != nullptr) {
        _runtime->navigateTo<T>();
    }
}

}  // namespace screenlib
