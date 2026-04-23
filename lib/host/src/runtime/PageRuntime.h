#pragma once

#include <cstddef>
#include <cstdint>

#include "pages/PageModel.h"
#include "proto/machine.pb.h"

namespace screenlib {

class IPage;              // fwd, определён ниже
class ScreenBridge;       // fwd
struct ScreenConfig;      // fwd

// --- Результат отправки set-команды через PageRuntime ---

// sendSetAttribute возвращает request_id, по которому позже придёт ACK.
// 0 означает сбой отправки (очередь переполнена или bridge.send()/provisioning не прошёл).
using RequestId = uint32_t;
constexpr RequestId kInvalidRequestId = 0;

// ------------------------------------------------------------------
// PageRuntime — единственный runtime объектной модели.
//
// Заменяет собой устаревший квартет ScreenSystem/ScreenManager/
// ScreenEndpoint/SinglePageRuntime.
//
// Задачи:
// - владеет одной или двумя ScreenBridge (physical/web);
// - держит ровно одну активную страницу (IPage);
// - ведёт локальную PageModel;
// - отправляет SetElementAttribute, ждёт ACK (AttributeChanged),
//   реализует backpressure;
// - дренирует очередь при navigateTo, ждёт PageSnapshot от экрана;
// - при тайм-ауте/переполнении уходит в linkDown.
//
// Полная реализация — в PageRuntime.cpp. Здесь только контракт.
// ------------------------------------------------------------------
class PageRuntime {
public:
    using LinkListener = void (*)(bool up, void* user);

    // --- Lifecycle ---

    // Инициализация транспорта по конфигу. Поднимает bridge(ы).
    // Возвращает true при успехе, false — с текстом в lastError().
    bool init(const ScreenConfig& cfg);

    // Главный tick. Прокачивает bridge(ы), проверяет таймауты очереди.
    void tick();

    // Переход на страницу типа T (должна наследоваться от IPage и иметь kPageId).
    template <typename T>
    bool navigateTo();

    // Вернуться на предыдущую страницу.
    bool back();

    // --- State ---

    bool linkUp() const { return _linkUp; }
    bool pageSynced() const { return _model.isReady() && _pendingCount == 0; }
    std::size_t pendingCommands() const { return _pendingCount; }

    void setLinkListener(LinkListener l, void* user) {
        _linkListener = l;
        _linkListenerUser = user;
    }

    // --- Для Element/Property (публично, т.к. вызывается из шаблонов) ---

    PageModel& model() { return _model; }
    const PageModel& model() const { return _model; }

    // Отправить SetElementAttribute с новым request_id.
    // Параметр v.attribute + v.which_value + v.value задают что менять.
    // При успехе возвращает request_id, кладёт запись в _pending.
    // При сбое (очередь переполнена, bridge не готов, send() провалился)
    // — возвращает kInvalidRequestId, помечает linkDown и сбрасывает запись.
    RequestId sendSetAttribute(uint32_t elementId, const ElementAttributeValue& v);

    // --- Для страницы ---

    IPage* currentPage() { return _current; }
    uint32_t currentPageId() const;
    uint32_t currentSessionId() const { return _model.sessionId(); }

private:
    // Здесь будет реальная реализация в .cpp. На этапе коммита 4 у нас только
    // контракт, чтобы Element.h мог компилиться.
    PageModel _model;
    IPage* _current = nullptr;

    // Заглушки полей (полный набор вводится в коммите 5).
    bool _linkUp = true;
    LinkListener _linkListener = nullptr;
    void* _linkListenerUser = nullptr;

    // Очередь ожидающих ACK.
    struct Pending {
        RequestId id = kInvalidRequestId;
        uint32_t elementId = 0;
        ElementAttribute attribute = ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
        uint32_t sentAtMs = 0;
    };
    static constexpr std::size_t kMaxPending = 64;
    Pending _pending[kMaxPending];
    std::size_t _pendingCount = 0;
    RequestId _nextRequestId = 1;
};

// ------------------------------------------------------------------
// IPage — базовый интерфейс страницы для PageRuntime.
//
// Конкретные страницы бэка наследуются от сгенерированных в ScreenUI
// base-классов (InfoPage<T>, MainPage<T> и т.д.), которые в свою
// очередь наследуются от IPage и зашивают kPageId.
// ------------------------------------------------------------------
class IPage {
    friend class PageRuntime;

public:
    virtual ~IPage() = default;
    virtual uint32_t pageId() const = 0;

protected:
    virtual void onShow() {}
    virtual void onClose() {}
    virtual void onTick() {}

    virtual void onButton(uint32_t elementId, ButtonAction action) {
        (void)elementId;
        (void)action;
    }
    virtual void onInputInt(uint32_t elementId, int32_t value) {
        (void)elementId;
        (void)value;
    }
    virtual void onInputText(uint32_t elementId, const char* text) {
        (void)elementId;
        (void)text;
    }

    PageRuntime* runtime() { return _runtime; }

private:
    PageRuntime* _runtime = nullptr;
    bool _shown = false;
};

}  // namespace screenlib
