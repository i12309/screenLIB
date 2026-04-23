#include <cstring>

#include <unity.h>

#include "pages/PageModel.h"

void setUp(void) {}
void tearDown(void) {}

using screenlib::AttributeValue;
using screenlib::PageModel;

namespace {

// ---------- Вспомогательные ----------

ElementAttributeValue makeInt(ElementAttribute a, int32_t v) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_int_value_tag;
    eav.value.int_value = v;
    return eav;
}

ElementAttributeValue makeBool(ElementAttribute a, bool v) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_bool_value_tag;
    eav.value.bool_value = v;
    return eav;
}

ElementAttributeValue makeColor(ElementAttribute a, uint32_t rgb888) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_color_value_tag;
    eav.value.color_value = rgb888;
    return eav;
}

ElementAttributeValue makeFont(ElementAttribute a, ElementFont f) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_font_value_tag;
    eav.value.font_value = f;
    return eav;
}

ElementAttributeValue makeString(ElementAttribute a, const char* s) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_string_value_tag;
    std::strncpy(eav.value.string_value, s, sizeof(eav.value.string_value) - 1);
    eav.value.string_value[sizeof(eav.value.string_value) - 1] = '\0';
    return eav;
}

AttributeChanged makeChanged(uint32_t pageId, uint32_t sessionId,
                             uint32_t elementId, const ElementAttributeValue& v,
                             AttributeChangeReason reason = AttributeChangeReason_REASON_COMMAND_APPLIED,
                             uint32_t inReplyTo = 0) {
    AttributeChanged m = AttributeChanged_init_zero;
    m.page_id = pageId;
    m.session_id = sessionId;
    m.element_id = elementId;
    m.has_value = true;
    m.value = v;
    m.reason = reason;
    m.in_reply_to_request = inReplyTo;
    return m;
}

// ---------- Тесты ----------

void test_begin_page_resets_state_and_remembers_ids() {
    PageModel m;
    m.setInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 42);
    TEST_ASSERT_TRUE(m.has(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));

    m.beginPage(7, 3);
    TEST_ASSERT_EQUAL_UINT32(7u, m.pageId());
    TEST_ASSERT_EQUAL_UINT32(3u, m.sessionId());
    TEST_ASSERT_FALSE(m.isReady());
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(m.slotCount()));
    TEST_ASSERT_FALSE(m.has(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_set_and_get_typed_attributes() {
    PageModel m;
    m.beginPage(1, 1);

    m.setInt   (10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 320);
    m.setBool  (10, ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE, true);
    m.setColor (10, ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR, 0x00AABBCCu);
    m.setFont  (10, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT, ElementFont_ELEMENT_FONT_UI_M24);
    m.setString(10, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT, "Hello");

    TEST_ASSERT_EQUAL_INT32(320, m.getInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
    TEST_ASSERT_TRUE(m.getBool(10, ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE));
    TEST_ASSERT_EQUAL_UINT32(0x00AABBCCu, m.getColor(10, ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR));
    TEST_ASSERT_EQUAL_INT(ElementFont_ELEMENT_FONT_UI_M24,
                          m.getFont(10, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT));
    const char* s = m.getString(10, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("Hello", s);
}

void test_get_returns_defaults_when_no_slot() {
    PageModel m;
    m.beginPage(1, 1);

    TEST_ASSERT_EQUAL_INT32(0, m.getInt(99, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
    TEST_ASSERT_FALSE(m.getBool(99, ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE));
    TEST_ASSERT_EQUAL_UINT32(0u, m.getColor(99, ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR));
    TEST_ASSERT_EQUAL_INT(ElementFont_ELEMENT_FONT_UNKNOWN,
                          m.getFont(99, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT));
    TEST_ASSERT_NULL(m.getString(99, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT));
    TEST_ASSERT_FALSE(m.has(99, ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE));
}

void test_repeated_set_updates_existing_slot() {
    PageModel m;
    m.beginPage(1, 1);

    m.setInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 100);
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(m.slotCount()));

    m.setInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 200);
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(m.slotCount()));
    TEST_ASSERT_EQUAL_INT32(200, m.getInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_color_is_masked_to_rgb888() {
    PageModel m;
    m.beginPage(1, 1);
    m.setColor(5, ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR, 0xFF112233u);
    TEST_ASSERT_EQUAL_UINT32(0x00112233u,
                             m.getColor(5, ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR));
}

void test_set_string_copies_into_pool() {
    PageModel m;
    m.beginPage(1, 1);

    char local[] = "mutable";
    m.setString(5, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT, local);
    local[0] = 'X';   // меняем исходник — модель не должна зависеть от него
    const char* stored = m.getString(5, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_STRING("mutable", stored);
}

void test_apply_snapshot_replaces_state() {
    PageModel m;
    m.beginPage(7, 2);
    // предварительное локальное состояние
    m.setInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 111);

    PageSnapshot snap = PageSnapshot_init_zero;
    snap.page_id = 7;
    snap.session_id = 2;
    snap.elements_count = 2;
    snap.elements[0].element_id = 1;
    snap.elements[0].attributes_count = 2;
    snap.elements[0].attributes[0] = makeInt(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 320);
    snap.elements[0].attributes[1] = makeBool(ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE, true);
    snap.elements[1].element_id = 2;
    snap.elements[1].attributes_count = 1;
    snap.elements[1].attributes[0] = makeString(ElementAttribute_ELEMENT_ATTRIBUTE_TEXT, "hello");

    m.applySnapshot(snap);

    TEST_ASSERT_EQUAL_INT32(320, m.getInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
    TEST_ASSERT_TRUE(m.getBool(1, ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE));
    TEST_ASSERT_EQUAL_STRING("hello", m.getString(2, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT));
    // Количество слотов = суммарно атрибутов в snapshot. Прежний слот width=111 вытеснен новым =320.
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(m.slotCount()));
}

void test_apply_remote_change_updates_slot() {
    PageModel m;
    m.beginPage(7, 5);

    m.setInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 100);

    AttributeChanged msg = makeChanged(
        7, 5, 10,
        makeInt(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 250),
        AttributeChangeReason_REASON_COMMAND_APPLIED,
        /*inReplyTo=*/42);
    m.applyRemoteChange(msg);

    TEST_ASSERT_EQUAL_INT32(250, m.getInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_apply_remote_change_stale_session_is_ignored() {
    PageModel m;
    m.beginPage(7, 5);
    m.setInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 100);

    AttributeChanged stale = makeChanged(
        7, /*sessionId=*/4, 10,
        makeInt(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 999));
    m.applyRemoteChange(stale);

    // Значение не должно измениться.
    TEST_ASSERT_EQUAL_INT32(100, m.getInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_apply_remote_change_stale_page_is_ignored() {
    PageModel m;
    m.beginPage(7, 5);
    m.setInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 100);

    AttributeChanged stale = makeChanged(
        /*pageId=*/6, 5, 10,
        makeInt(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 999));
    m.applyRemoteChange(stale);

    TEST_ASSERT_EQUAL_INT32(100, m.getInt(10, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_mark_ready_flag() {
    PageModel m;
    m.beginPage(1, 1);
    TEST_ASSERT_FALSE(m.isReady());
    m.markReady();
    TEST_ASSERT_TRUE(m.isReady());
    m.beginPage(2, 2);
    TEST_ASSERT_FALSE(m.isReady());
}

void test_slot_overflow_drops_but_does_not_crash() {
    PageModel m;
    m.beginPage(1, 1);

    // Забиваем все слоты уникальными (element_id, attribute) парами.
    // Используем один attribute, разные element_id.
    for (uint32_t i = 0; i < PageModel::kMaxSlots; ++i) {
        m.setInt(i + 1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH,
                 static_cast<int32_t>(i));
    }
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PageModel::kMaxSlots),
                             static_cast<uint32_t>(m.slotCount()));

    // Ещё один — должен быть дропнут без падения.
    m.setInt(99999, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 777);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(PageModel::kMaxSlots),
                             static_cast<uint32_t>(m.slotCount()));
    TEST_ASSERT_FALSE(m.has(99999, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));

    // Но повторная запись в уже существующий слот должна работать.
    m.setInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 12345);
    TEST_ASSERT_EQUAL_INT32(12345, m.getInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_clear_fully_resets_model() {
    PageModel m;
    m.beginPage(7, 2);
    m.setInt(1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 42);
    m.setString(1, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT, "txt");
    m.markReady();

    m.clear();
    TEST_ASSERT_EQUAL_UINT32(0u, m.pageId());
    TEST_ASSERT_EQUAL_UINT32(0u, m.sessionId());
    TEST_ASSERT_FALSE(m.isReady());
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(m.slotCount()));
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(m.stringPoolUsed()));
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_begin_page_resets_state_and_remembers_ids);
    RUN_TEST(test_set_and_get_typed_attributes);
    RUN_TEST(test_get_returns_defaults_when_no_slot);
    RUN_TEST(test_repeated_set_updates_existing_slot);
    RUN_TEST(test_color_is_masked_to_rgb888);
    RUN_TEST(test_set_string_copies_into_pool);
    RUN_TEST(test_apply_snapshot_replaces_state);
    RUN_TEST(test_apply_remote_change_updates_slot);
    RUN_TEST(test_apply_remote_change_stale_session_is_ignored);
    RUN_TEST(test_apply_remote_change_stale_page_is_ignored);
    RUN_TEST(test_mark_ready_flag);
    RUN_TEST(test_slot_overflow_drops_but_does_not_crash);
    RUN_TEST(test_clear_fully_resets_model);
}

#ifdef ARDUINO
void setup() {
    UNITY_BEGIN();
    run_all_tests();
    UNITY_END();
}

void loop() {}
#else
int main() {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
#endif
