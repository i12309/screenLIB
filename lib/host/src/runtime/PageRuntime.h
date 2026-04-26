#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "chunk/TextChunkAssembler.h"
#include "pages/PageModel.h"
#include "proto/machine.pb.h"
#include "types/ScreenTypes.h"

class ScreenBridge;  // fwd (лежит в global namespace, см. lib/core/src/bridge)

namespace screenlib {

struct ScreenConfig;  // fwd
class IPage;          // определён ниже

// --- Результат отправки set-команды через PageRuntime ---

// sendSetAttribute возвращает request_id, по которому позже придёт ACK.
// 0 означает сбой отправки (очередь переполнена или bridge.send() не прошёл).
using RequestId = uint32_t;
constexpr RequestId kInvalidRequestId = 0;

// ------------------------------------------------------------------
// PageRuntime — единственный runtime объектной модели страницы.
//
// Задачи:
// - держит ровно одну активную страницу (IPage);
// - ведёт локальную PageModel (каноничное состояние страницы);
// - отправляет SetElementAttribute, ждёт ACK (AttributeChanged),
//   реализует backpressure;
// - при navigateTo дренирует очередь, шлёт ShowPage, ждёт PageSnapshot;
// - при таймауте/переполнении уходит в linkDown и уведомляет слушателя.
//
// Bridge (физический/web) сейчас привязываются извне через
// attachPhysicalBridge/attachWebBridge. Автоподнятие из ScreenConfig
// (UART/WsServer) будет добавлено в отдельном коммите.
// ------------------------------------------------------------------
class PageRuntime {
public:
    using LinkListener = void (*)(bool up, void* user);
    using DeviceInfoListener = void (*)(const DeviceInfo& info, void* user);
    using PageFactory  = std::unique_ptr<IPage> (*)();

    // --- Тайминги/лимиты ---

    // Максимум отправленных команд без ACK. При превышении — linkDown.
    static constexpr std::size_t kMaxPending = 64;
    // Таймаут ожидания ACK на самую старую команду в очереди.
    static constexpr uint32_t kLinkTimeoutMs = 2000;

    PageRuntime() = default;
    PageRuntime(const PageRuntime&) = delete;
    PageRuntime& operator=(const PageRuntime&) = delete;

    // --- Lifecycle ---

    // Сохранить конфиг (mirrorMode и прочее). bridge(и) привязываются
    // отдельно через attachPhysicalBridge/attachWebBridge.
    bool init(const ScreenConfig& cfg);

    // Главный tick. Прокачивает bridge(ы), проверяет таймауты очереди
    // и текущую страницу (onTick).
    void tick();

    // Привязать bridge физического экрана. Устанавливает handler входящих.
    void attachPhysicalBridge(ScreenBridge* bridge);
    // Привязать bridge web-экрана.
    void attachWebBridge(ScreenBridge* bridge);

    // Переход на страницу типа T. T должна наследоваться от IPage
    // и иметь static constexpr uint32_t kPageId.
    template <typename T>
    bool navigateTo() {
        static_assert(std::is_base_of<IPage, T>::value,
                      "navigate target must inherit from screenlib::IPage");
        return swapCurrent(std::unique_ptr<IPage>(new T()), &makePage<T>, T::kPageId);
    }

    // Вернуться на предыдущую страницу (если была). Эпоха не меняется
    // только если это был тот же pageId; в остальных случаях — новая сессия.
    bool back();

    // --- State ---

    bool linkUp() const { return _linkUp; }
    bool pageSynced() const { return _model.isReady() && _pendingCount == 0; }
    std::size_t pendingCommands() const { return _pendingCount; }

    void setLinkListener(LinkListener l, void* user) {
        _linkListener = l;
        _linkListenerUser = user;
    }

    void setDeviceInfoListener(DeviceInfoListener l, void* user) {
        _deviceInfoListener = l;
        _deviceInfoListenerUser = user;
    }

    // --- Для тестов: инжекция источника времени ---

    // По умолчанию — Arduino millis() на ARDUINO-сборках и
    // std::chrono::steady_clock в нативных. setNowProvider переопределяет.
    using NowProvider = uint32_t (*)();
    void setNowProvider(NowProvider p) { _now = p; }

    // --- Для Element/Property (публично, т.к. вызывается из шаблонов) ---

    PageModel& model() { return _model; }
    const PageModel& model() const { return _model; }

    // Отправить SetElementAttribute с новым request_id.
    // При успехе возвращает request_id, кладёт запись в _pending.
    // При сбое (очередь переполнена, bridge не готов, send() провалился)
    // — возвращает kInvalidRequestId, помечает linkDown.
    RequestId sendSetAttribute(uint32_t elementId, const ElementAttributeValue& v);

    // --- Для страницы ---

    IPage* currentPage() { return _current.get(); }
    const IPage* currentPage() const { return _current.get(); }
    uint32_t currentPageId() const;
    uint32_t currentSessionId() const { return _model.sessionId(); }

private:
    template <typename T>
    static std::unique_ptr<IPage> makePage() {
        return std::unique_ptr<IPage>(new T());
    }

    // Ядро навигации: дренаж pending, onClose старой, установка новой,
    // beginPage в модели, отправка ShowPage.
    bool swapCurrent(std::unique_ptr<IPage> next, PageFactory nextFactory, uint32_t pageId);

    // Диспетчеризация входящего Envelope (колбэк от ScreenBridge).
    static void onBridgeEnvelope(const Envelope& env, void* userData);
    void onEnvelope(const Envelope& env);

    bool sendShowPageByMode(uint32_t pageId, uint32_t sessionId);
    bool sendSetElementAttributeByMode(const SetElementAttribute& cmd);
    bool sendTextChunksByMode(TextChunkKind kind,
                              uint32_t transferId,
                              uint32_t sessionId,
                              uint32_t pageId,
                              uint32_t elementId,
                              ElementAttribute attribute,
                              uint32_t requestId,
                              const char* text);
    bool sendTextChunkAbortByMode(const TextChunkAbort& abort);
    void notifyDeviceInfo(const DeviceInfo& info);

    // Проверка таймаута на голове очереди. Вызывается из tick.
    void checkPendingTimeouts();

    // Пометить линк как up/down, позвать listener при изменении.
    void setLinkUp(bool up);

    // Удалить pending по request_id (линейный поиск). true если нашёл.
    bool removePending(RequestId id);

    // Получить текущее время (через _now или default).
    uint32_t nowMs() const;

    // --- Состояние ---

    MirrorMode _mirrorMode = MirrorMode::PhysicalOnly;
    ScreenBridge* _physical = nullptr;
    ScreenBridge* _web = nullptr;

    PageModel _model;
    std::unique_ptr<IPage> _current;
    PageFactory _currentFactory = nullptr;
    PageFactory _previousFactory = nullptr;
    uint32_t _sessionCounter = 0;  // монотонно растёт, эпоха = _sessionCounter

    bool _linkUp = true;
    LinkListener _linkListener = nullptr;
    void* _linkListenerUser = nullptr;
    DeviceInfoListener _deviceInfoListener = nullptr;
    void* _deviceInfoListenerUser = nullptr;

    struct Pending {
        RequestId id = kInvalidRequestId;
        uint32_t elementId = 0;
        ElementAttribute attribute = ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
        uint32_t sentAtMs = 0;
    };
    Pending _pending[kMaxPending];
    std::size_t _pendingCount = 0;
    RequestId _nextRequestId = 1;
    uint32_t _nextTransferId = 1;
    screenlib::chunk::TextChunkAssembler _textAssembler;

    NowProvider _now = nullptr;  // nullptr → использовать default monotonic_ms
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
    PageRuntime* runtime() { return _runtime; }
    const PageRuntime* runtime() const { return _runtime; }

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

private:
    PageRuntime* _runtime = nullptr;
    bool _shown = false;
};

}  // namespace screenlib
