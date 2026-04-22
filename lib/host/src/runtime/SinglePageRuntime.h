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
    // Фабрика страницы для системного возврата на предыдущий экран.
    using PageFactory = std::unique_ptr<IHostPage> (*)();
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
        return swapCurrent(makePage<T>(), &makePage<T>);
    }

    // Возвращает предыдущую страницу, если она была сохранена runtime.
    bool back();

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
    // Универсальные типизированные set/get по атрибуту.
    bool setElementAttribute(const SetElementAttribute& attr) { return _screens.setElementAttribute(attr); }
    bool requestElementAttribute(uint32_t elementId,
                                 ElementAttribute attribute,
                                 uint32_t pageId = 0,
                                 uint32_t requestId = 0) {
        return _screens.requestElementAttribute(elementId, attribute, pageId, requestId);
    }
    // Типизированные вспомогательные методы записи.
    bool setElementWidth(uint32_t elementId, int32_t value) { return _screens.setElementWidth(elementId, value); }
    bool setElementHeight(uint32_t elementId, int32_t value) { return _screens.setElementHeight(elementId, value); }
    bool setElementBackgroundColor(uint32_t elementId, uint32_t rgb888) {
        return _screens.setElementBackgroundColor(elementId, rgb888);
    }
    bool setElementBorderColor(uint32_t elementId, uint32_t rgb888) {
        return _screens.setElementBorderColor(elementId, rgb888);
    }
    bool setElementBorderWidth(uint32_t elementId, int32_t value) {
        return _screens.setElementBorderWidth(elementId, value);
    }
    bool setElementTextColor(uint32_t elementId, uint32_t rgb888) {
        return _screens.setElementTextColor(elementId, rgb888);
    }
    bool setElementTextFont(uint32_t elementId, ElementFont font) {
        return _screens.setElementTextFont(elementId, font);
    }
    // Типизированные вспомогательные методы запроса значений.
    bool requestElementWidth(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementWidth(elementId, pageId, requestId);
    }
    bool requestElementHeight(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementHeight(elementId, pageId, requestId);
    }
    bool requestElementBackgroundColor(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementBackgroundColor(elementId, pageId, requestId);
    }
    bool requestElementBorderColor(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementBorderColor(elementId, pageId, requestId);
    }
    bool requestElementBorderWidth(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementBorderWidth(elementId, pageId, requestId);
    }
    bool requestElementTextColor(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementTextColor(elementId, pageId, requestId);
    }
    bool requestElementTextFont(uint32_t elementId, uint32_t pageId = 0, uint32_t requestId = 0) {
        return _screens.requestElementTextFont(elementId, pageId, requestId);
    }

private:
    // Создает экземпляр страницы указанного типа.
    template <typename T>
    static std::unique_ptr<IHostPage> makePage() {
        return std::unique_ptr<IHostPage>(new T());
    }

    static void onScreenEvent(const Envelope& env, const ScreenEventContext& ctx, void* userData);
    void dispatch(const Envelope& env, const ScreenEventContext& ctx);
    bool swapCurrent(std::unique_ptr<IHostPage> next, PageFactory nextFactory);

    ScreenSystem _screens;
    std::unique_ptr<IHostPage> _current;
    PageFactory _currentFactory = nullptr;
    PageFactory _previousFactory = nullptr;
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
