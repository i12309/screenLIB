#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "lvgl_eez/EezLvglAdapter.h"
#include "lvgl_eez/UiObjectMap.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

struct EventCapture {
    int count = 0;
    Envelope last = Envelope_init_zero;
} gEventCapture;

bool onEventSink(const Envelope& env, void* userData) {
    EventCapture* capture = static_cast<EventCapture*>(userData);
    if (capture == nullptr) {
        return false;
    }
    capture->count++;
    capture->last = env;
    return true;
}

struct FakeBackend {
    int showPageCalls = 0;
    int setTextCalls = 0;
    int setValueCalls = 0;
    int setVisibleCalls = 0;
    int setColorCalls = 0;
    int tickInputCalls = 0;

    void* lastPageTarget = nullptr;
    void* lastObject = nullptr;
    int32_t lastValue = 0;
    bool lastVisible = false;
    uint32_t lastBgColor = 0;
    uint32_t lastFgColor = 0;
    char lastText[32] = {};

    static bool showPage(void* userData, void* pageTarget) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->showPageCalls++;
        self->lastPageTarget = pageTarget;
        return true;
    }

    static bool setText(void* userData, void* uiObject, const char* text) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->setTextCalls++;
        self->lastObject = uiObject;
        strncpy(self->lastText, text != nullptr ? text : "", sizeof(self->lastText) - 1);
        self->lastText[sizeof(self->lastText) - 1] = '\0';
        return true;
    }

    static bool setValue(void* userData, void* uiObject, int32_t value) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->setValueCalls++;
        self->lastObject = uiObject;
        self->lastValue = value;
        return true;
    }

    static bool setVisible(void* userData, void* uiObject, bool visible) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->setVisibleCalls++;
        self->lastObject = uiObject;
        self->lastVisible = visible;
        return true;
    }

    static bool setColor(void* userData, void* uiObject, uint32_t bgColor, uint32_t fgColor) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->setColorCalls++;
        self->lastObject = uiObject;
        self->lastBgColor = bgColor;
        self->lastFgColor = fgColor;
        return true;
    }

    static void tickInput(void* userData, screenlib::adapter::EezLvglAdapter& adapter) {
        FakeBackend* self = static_cast<FakeBackend*>(userData);
        self->tickInputCalls++;
        adapter.emitButtonEvent(77, 9);
    }
};

void test_ui_object_map_binds_and_finds() {
    screenlib::adapter::UiObjectBinding objects[4] = {};
    screenlib::adapter::UiPageBinding pages[2] = {};
    screenlib::adapter::UiObjectMap map(objects, 4, pages, 2);

    void* objA = reinterpret_cast<void*>(0x1001);
    void* objB = reinterpret_cast<void*>(0x1002);
    void* pageA = reinterpret_cast<void*>(0x2001);

    TEST_ASSERT_TRUE(map.bindElement(10, objA));
    TEST_ASSERT_TRUE(map.bindElement(11, objB));
    TEST_ASSERT_TRUE(map.bindPage(1, pageA));

    TEST_ASSERT_EQUAL_PTR(objA, map.findElement(10));
    TEST_ASSERT_EQUAL_PTR(objB, map.findElement(11));
    TEST_ASSERT_EQUAL_PTR(pageA, map.findPage(1));

    TEST_ASSERT_NULL(map.findElement(999));
    TEST_ASSERT_NULL(map.findPage(999));
}

void test_eez_lvgl_adapter_show_page_uses_page_map() {
    screenlib::adapter::UiObjectBinding objects[1] = {};
    screenlib::adapter::UiPageBinding pages[2] = {};
    screenlib::adapter::UiObjectMap map(objects, 1, pages, 2);

    FakeBackend backend;
    screenlib::adapter::EezLvglHooks hooks{};
    hooks.showPage = &FakeBackend::showPage;

    screenlib::adapter::EezLvglAdapter adapter(&map, hooks, &backend);

    void* pageTarget = reinterpret_cast<void*>(0x3001);
    TEST_ASSERT_TRUE(map.bindPage(5, pageTarget));

    TEST_ASSERT_TRUE(adapter.showPage(5));
    TEST_ASSERT_EQUAL_INT(1, backend.showPageCalls);
    TEST_ASSERT_EQUAL_PTR(pageTarget, backend.lastPageTarget);

    TEST_ASSERT_FALSE(adapter.showPage(6));
}

void test_eez_lvgl_adapter_set_commands_work_for_known_objects() {
    screenlib::adapter::UiObjectBinding objects[4] = {};
    screenlib::adapter::UiPageBinding pages[1] = {};
    screenlib::adapter::UiObjectMap map(objects, 4, pages, 1);

    FakeBackend backend;
    screenlib::adapter::EezLvglHooks hooks{};
    hooks.setText = &FakeBackend::setText;
    hooks.setValue = &FakeBackend::setValue;
    hooks.setVisible = &FakeBackend::setVisible;
    hooks.setColor = &FakeBackend::setColor;

    screenlib::adapter::EezLvglAdapter adapter(&map, hooks, &backend);

    void* obj = reinterpret_cast<void*>(0x4001);
    TEST_ASSERT_TRUE(map.bindElement(100, obj));

    TEST_ASSERT_TRUE(adapter.setText(100, "ok"));
    TEST_ASSERT_EQUAL_INT(1, backend.setTextCalls);
    TEST_ASSERT_EQUAL_STRING("ok", backend.lastText);

    TEST_ASSERT_TRUE(adapter.setValue(100, 42));
    TEST_ASSERT_EQUAL_INT(1, backend.setValueCalls);
    TEST_ASSERT_EQUAL_INT32(42, backend.lastValue);

    TEST_ASSERT_TRUE(adapter.setVisible(100, true));
    TEST_ASSERT_EQUAL_INT(1, backend.setVisibleCalls);
    TEST_ASSERT_TRUE(backend.lastVisible);

    TEST_ASSERT_TRUE(adapter.setColor(100, 0x11, 0x22));
    TEST_ASSERT_EQUAL_INT(1, backend.setColorCalls);
    TEST_ASSERT_EQUAL_UINT32(0x11, backend.lastBgColor);
    TEST_ASSERT_EQUAL_UINT32(0x22, backend.lastFgColor);

    TEST_ASSERT_FALSE(adapter.setText(101, "missing"));
}

void test_eez_lvgl_adapter_apply_batch_is_sequential_and_reports_failures() {
    screenlib::adapter::UiObjectBinding objects[3] = {};
    screenlib::adapter::UiPageBinding pages[1] = {};
    screenlib::adapter::UiObjectMap map(objects, 3, pages, 1);

    FakeBackend backend;
    screenlib::adapter::EezLvglHooks hooks{};
    hooks.setText = &FakeBackend::setText;
    hooks.setValue = &FakeBackend::setValue;
    hooks.setVisible = &FakeBackend::setVisible;
    hooks.setColor = &FakeBackend::setColor;

    screenlib::adapter::EezLvglAdapter adapter(&map, hooks, &backend);

    TEST_ASSERT_TRUE(map.bindElement(1, reinterpret_cast<void*>(0x5001)));
    TEST_ASSERT_TRUE(map.bindElement(2, reinterpret_cast<void*>(0x5002)));

    SetBatch batch = SetBatch_init_zero;
    batch.texts_count = 2;
    batch.texts[0].element_id = 1;
    strncpy(batch.texts[0].text, "a", sizeof(batch.texts[0].text) - 1);
    batch.texts[1].element_id = 999;  // отсутствует в map
    strncpy(batch.texts[1].text, "b", sizeof(batch.texts[1].text) - 1);

    batch.values_count = 1;
    batch.values[0].element_id = 2;
    batch.values[0].value = 77;

    const bool ok = adapter.applyBatch(batch);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(1, backend.setTextCalls);   // только element_id=1
    TEST_ASSERT_EQUAL_INT(1, backend.setValueCalls);  // продолжили после ошибки
    TEST_ASSERT_EQUAL_INT32(77, backend.lastValue);
}

void test_eez_lvgl_adapter_emits_only_user_events_to_sink() {
    screenlib::adapter::UiObjectBinding objects[1] = {};
    screenlib::adapter::UiPageBinding pages[1] = {};
    screenlib::adapter::UiObjectMap map(objects, 1, pages, 1);

    screenlib::adapter::EezLvglAdapter adapter(&map);

    gEventCapture = EventCapture{};
    adapter.setEventSink(&onEventSink, &gEventCapture);

    TEST_ASSERT_TRUE(adapter.emitButtonEvent(10, 2));
    TEST_ASSERT_EQUAL_INT(1, gEventCapture.count);
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, gEventCapture.last.which_payload);
    TEST_ASSERT_EQUAL_UINT32(10, gEventCapture.last.payload.button_event.element_id);
    TEST_ASSERT_EQUAL_UINT32(2, gEventCapture.last.payload.button_event.page_id);

    TEST_ASSERT_TRUE(adapter.emitInputEventInt(11, 2, 123));
    TEST_ASSERT_EQUAL_INT(2, gEventCapture.count);
    TEST_ASSERT_EQUAL_UINT32(Envelope_input_event_tag, gEventCapture.last.which_payload);
    TEST_ASSERT_EQUAL_UINT32(InputEvent_int_value_tag, gEventCapture.last.payload.input_event.which_value);

    TEST_ASSERT_TRUE(adapter.emitInputEventString(12, 2, "text"));
    TEST_ASSERT_EQUAL_INT(3, gEventCapture.count);
    TEST_ASSERT_EQUAL_UINT32(Envelope_input_event_tag, gEventCapture.last.which_payload);
    TEST_ASSERT_EQUAL_UINT32(InputEvent_string_value_tag, gEventCapture.last.payload.input_event.which_value);
    TEST_ASSERT_EQUAL_STRING("text", gEventCapture.last.payload.input_event.value.string_value);
}

void test_eez_lvgl_adapter_tick_input_uses_hook() {
    screenlib::adapter::UiObjectBinding objects[1] = {};
    screenlib::adapter::UiPageBinding pages[1] = {};
    screenlib::adapter::UiObjectMap map(objects, 1, pages, 1);

    FakeBackend backend;
    screenlib::adapter::EezLvglHooks hooks{};
    hooks.tickInput = &FakeBackend::tickInput;

    screenlib::adapter::EezLvglAdapter adapter(&map, hooks, &backend);

    gEventCapture = EventCapture{};
    adapter.setEventSink(&onEventSink, &gEventCapture);

    adapter.tickInput();
    TEST_ASSERT_EQUAL_INT(1, backend.tickInputCalls);
    TEST_ASSERT_EQUAL_INT(1, gEventCapture.count);
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, gEventCapture.last.which_payload);
    TEST_ASSERT_EQUAL_UINT32(77, gEventCapture.last.payload.button_event.element_id);
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_ui_object_map_binds_and_finds);
    RUN_TEST(test_eez_lvgl_adapter_show_page_uses_page_map);
    RUN_TEST(test_eez_lvgl_adapter_set_commands_work_for_known_objects);
    RUN_TEST(test_eez_lvgl_adapter_apply_batch_is_sequential_and_reports_failures);
    RUN_TEST(test_eez_lvgl_adapter_emits_only_user_events_to_sink);
    RUN_TEST(test_eez_lvgl_adapter_tick_input_uses_hook);
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
