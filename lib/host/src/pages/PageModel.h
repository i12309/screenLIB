#pragma once

#include <cstddef>
#include <cstdint>

#include "proto/machine.pb.h"

namespace screenlib {

// Значение одного атрибута элемента в локальной модели страницы.
// Хранится "by value" в слоте PageModel. Для строк указатель s
// всегда указывает на внутренний пул страницы (см. PageModel::_stringPool).
struct AttributeValue {
    enum class Type : uint8_t {
        None,
        Int,
        Bool,
        Color,
        Font,
        String
    };

    Type type = Type::None;
    int32_t i = 0;
    uint32_t u = 0;
    bool b = false;
    ElementFont font = ElementFont_ELEMENT_FONT_UNKNOWN;
    const char* s = nullptr;  // валидно пока жив буфер страницы
};

// Локальная модель состояния одной страницы.
//
// На бэке хранит каноничный слепок атрибутов всех элементов активной страницы.
// Чтение — O(N) по плоскому массиву слотов (N = элементы × атрибуты на странице).
// Запись — либо апдейт существующего слота, либо вставка нового.
// Память — полностью статическая: никаких аллокаций, фиксированный буфер слотов
// и фиксированный пул строк.
//
// Жизненный цикл:
//   beginPage(pageId, sessionId) — очистка + запоминание эпохи навигации.
//   applySnapshot(snap)          — приём полного слепка от экрана.
//   applyRemoteChange(msg)       — приём точечной дельты от экрана.
//   setInt/setBool/...           — локальная запись бэком (оптимистичная).
//   getInt/getBool/...           — чтение бэком.
//
// Важно: PageModel **не** отправляет сообщения наружу. Его задача — хранить
// состояние. Все сетевые операции (очередь ACK, отправка SetElementAttribute)
// делает PageRuntime, используя PageModel как источник истины.
class PageModel {
public:
    // Предел слотов на страницу. 256 × ~12 байт = 3 КБ.
    // Для старта хватит с запасом: типичная страница 20 элементов × 10 атрибутов = 200 слотов.
    static constexpr std::size_t kMaxSlots = 256;

    // Пул строк на страницу. Всё, что пишется через setString/applySnapshot/applyRemoteChange,
    // копируется сюда. При beginPage пул обнуляется.
    static constexpr std::size_t kStringPoolSize = 2048;

    // --- Жизненный цикл ---

    // Подготовить модель к приёму данных новой страницы.
    // sessionId запоминается для диагностики и согласованности чтений.
    void beginPage(uint32_t pageId, uint32_t sessionId);

    // Полностью очистить состояние (pageId, sessionId, слоты, пул строк).
    void clear();

    // Применить полный снимок от экрана. Предварительно очищает все слоты
    // и пул строк, но pageId/sessionId берутся из snap (ожидается, что они
    // согласованы с beginPage; рассогласование логируется).
    void applySnapshot(const PageSnapshot& snap);

    // Применить точечную дельту от экрана (экран канонично прав).
    // session_id/page_id в msg обязан совпадать с текущим, иначе дельта
    // игнорируется и логируется как stale.
    void applyRemoteChange(const AttributeChanged& msg);

    // --- Read API (типизированное) ---
    // Если слота нет, возвращают default (0/false/nullptr).

    int32_t     getInt   (uint32_t elementId, ElementAttribute a) const;
    bool        getBool  (uint32_t elementId, ElementAttribute a) const;
    uint32_t    getColor (uint32_t elementId, ElementAttribute a) const;
    ElementFont getFont  (uint32_t elementId, ElementAttribute a) const;
    const char* getString(uint32_t elementId, ElementAttribute a) const;

    // Проверить, существует ли слот под (element, attribute).
    bool has(uint32_t elementId, ElementAttribute a) const;

    using SlotVisitor = void (*)(uint32_t elementId,
                                 ElementAttribute attribute,
                                 const AttributeValue& value,
                                 void* user);
    void forEachSlot(SlotVisitor visitor, void* user) const;

    // --- Write API (локальная оптимистичная запись) ---

    void setInt   (uint32_t elementId, ElementAttribute a, int32_t v);
    void setBool  (uint32_t elementId, ElementAttribute a, bool v);
    void setColor (uint32_t elementId, ElementAttribute a, uint32_t rgb888);
    void setFont  (uint32_t elementId, ElementAttribute a, ElementFont v);
    void setString(uint32_t elementId, ElementAttribute a, const char* v);

    // --- Мета ---

    uint32_t pageId()    const { return _pageId; }
    uint32_t sessionId() const { return _sessionId; }
    bool     isReady()   const { return _ready; }    // true после applySnapshot
    std::size_t slotCount() const { return _slotCount; }
    std::size_t stringPoolUsed() const { return _stringPoolUsed; }

    // Пометить модель как готовую (вызывается PageRuntime после успешного snapshot).
    void markReady() { _ready = true; }

private:
    struct Slot {
        uint32_t elementId;
        ElementAttribute attribute;
        AttributeValue value;
    };

    uint32_t _pageId = 0;
    uint32_t _sessionId = 0;
    bool _ready = false;

    Slot _slots[kMaxSlots];
    std::size_t _slotCount = 0;

    char _stringPool[kStringPoolSize];
    std::size_t _stringPoolUsed = 0;

    // Внутренние операции поиска/вставки.
    Slot* findSlot(uint32_t elementId, ElementAttribute a);
    const Slot* findSlot(uint32_t elementId, ElementAttribute a) const;
    Slot* insertSlot(uint32_t elementId, ElementAttribute a);

    // Скопировать строку в пул и вернуть указатель.
    // При переполнении пула — обрезка, возврат валидного указателя (возможно, пустого).
    const char* internString(const char* s);

    // Применить одно ElementAttributeValue к (element, attribute).
    // Используется из applySnapshot/applyRemoteChange.
    void applyValue(uint32_t elementId, const ElementAttributeValue& v);

    // Очистить все слоты и пул строк без сброса pageId/sessionId.
    void resetStorage();
};

}  // namespace screenlib
