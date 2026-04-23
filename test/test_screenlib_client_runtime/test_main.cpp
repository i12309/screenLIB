#include <deque>
#include <string.h>
#include <vector>

#include <unity.h>

#include "frame/FrameCodec.h"
#include "proto/ProtoCodec.h"
#include "runtime/ScreenClient.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// Простой mock-транспорт для runtime-тестов.
class MockTransport : public ITransport {
public:
    bool isConnected = true;
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;

    bool connected() const override { return isConnected; }

    bool write(const uint8_t* data, size_t len) override {
        if (!isConnected) return false;
        tx.insert(tx.end(), data, data + len);
        return true;
    }

    size_t read(uint8_t* dst, size_t max_len) override {
        size_t n = 0;
        while (n < max_len && !rx.empty()) {
            dst[n++] = rx.front();
            rx.pop_front();
        }
        return n;
    }

    void tick() override {}

    void pushRx(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            rx.push_back(data[i]);
        }
    }
};

bool buildFrameFromEnvelope(const Envelope& env, std::vector<uint8_t>& outFrame, uint8_t seq = 1) {
    uint8_t protoBuf[ProtoCodec::kMaxEncodedSize] = {};
    const size_t protoLen = ProtoCodec::encode(env, protoBuf, sizeof(protoBuf));
    if (protoLen == 0) return false;

    uint8_t frameBuf[kMaxPayload + kFrameOverhead] = {};
    const size_t frameLen = FrameCodec::pack(
        protoBuf,
        static_cast<uint16_t>(protoLen),
        frameBuf,
        sizeof(frameBuf),
        seq,
        kFrameVer
    );
    if (frameLen == 0) return false;

    outFrame.assign(frameBuf, frameBuf + frameLen);
    return true;
}

bool pushIncomingEnvelope(MockTransport& transport, const Envelope& env, uint8_t seq = 1) {
    std::vector<uint8_t> frame;
    if (!buildFrameFromEnvelope(env, frame, seq)) {
        return false;
    }
    transport.pushRx(frame.data(), frame.size());
    return true;
}

size_t decodeAllTxEnvelopes(const MockTransport& transport, std::vector<Envelope>& out) {
    out.clear();
    if (transport.tx.empty()) return 0;

    FrameCodec codec;
    codec.feed(transport.tx.data(), transport.tx.size());

    FrameCodec::Frame frame;
    while (codec.popFrame(frame)) {
        Envelope env{};
        if (ProtoCodec::decode(frame.payload, frame.payloadLen, env)) {
            out.push_back(env);
        }
    }

    return out.size();
}

class FakeUiAdapter : public screenlib::adapter::IUiAdapter {
public:
    bool showPage(uint32_t pageId) override {
        showPageCount++;
        lastPageId = pageId;
        return true;
    }

    bool setText(uint32_t elementId, const char* text) override {
        setTextCount++;
        lastTextElementId = elementId;
        copyTextSafe(lastText, sizeof(lastText), text);
        return true;
    }

    bool setValue(uint32_t elementId, int32_t value) override {
        setValueCount++;
        lastValueElementId = elementId;
        lastValue = value;
        return true;
    }

    bool setVisible(uint32_t elementId, bool visible) override {
        setVisibleCount++;
        lastVisibleElementId = elementId;
        lastVisible = visible;
        return true;
    }

    bool setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) override {
        setColorCount++;
        lastColorElementId = elementId;
        lastBgColor = bgColor;
        lastFgColor = fgColor;
        return true;
    }

    bool setElementAttribute(const SetElementAttribute& attr) override {
        setElementAttributeCount++;
        lastElementAttribute = attr;
        return true;
    }

    bool applyBatch(const SetBatch& batch) override {
        applyBatchCount++;
        lastBatch = batch;
        return true;
    }

    void setEventSink(EventSink sink, void* userData) override {
        setEventSinkCalls++;
        if (sink == nullptr) {
            clearEventSinkCalls++;
        }
        _sink = sink;
        _sinkUser = userData;
    }

    void tickInput() override {
        tickInputCalls++;

        for (size_t i = 0; i < _pendingEvents.size(); ++i) {
            bool ok = false;
            if (_sink != nullptr) {
                ok = _sink(_pendingEvents[i], _sinkUser);
            }
            emitResults.push_back(ok);
        }
        _pendingEvents.clear();
    }

    void queueEvent(const Envelope& env) {
        _pendingEvents.push_back(env);
    }

    bool emitNow(const Envelope& env) {
        if (_sink == nullptr) return false;
        return _sink(env, _sinkUser);
    }

    EventSink currentSink() const { return _sink; }

    int showPageCount = 0;
    int setTextCount = 0;
    int setValueCount = 0;
    int setVisibleCount = 0;
    int setColorCount = 0;
    int setElementAttributeCount = 0;
    int applyBatchCount = 0;
    int setEventSinkCalls = 0;
    int clearEventSinkCalls = 0;
    int tickInputCalls = 0;

    uint32_t lastPageId = 0;
    uint32_t lastTextElementId = 0;
    char lastText[64] = {};
    uint32_t lastValueElementId = 0;
    int32_t lastValue = 0;
    uint32_t lastVisibleElementId = 0;
    bool lastVisible = false;
    uint32_t lastColorElementId = 0;
    uint32_t lastBgColor = 0;
    uint32_t lastFgColor = 0;
    SetElementAttribute lastElementAttribute = SetElementAttribute_init_zero;
    SetBatch lastBatch = SetBatch_init_zero;
    std::vector<bool> emitResults;

private:
    static void copyTextSafe(char* dst, size_t dstSize, const char* src) {
        if (dst == nullptr || dstSize == 0) return;
        dst[0] = '\0';
        if (src == nullptr) return;
        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    EventSink _sink = nullptr;
    void* _sinkUser = nullptr;
    std::vector<Envelope> _pendingEvents;
};

Envelope makeButtonEvent(uint32_t elementId, uint32_t pageId, ButtonAction action = ButtonAction_CLICK) {
    Envelope env{};
    env.which_payload = Envelope_button_event_tag;
    env.payload.button_event.element_id = elementId;
    env.payload.button_event.page_id = pageId;
    env.payload.button_event.action = action;
    return env;
}

Envelope makeInputEventInt(uint32_t elementId, uint32_t pageId, int32_t value) {
    Envelope env{};
    env.which_payload = Envelope_input_event_tag;
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_int_value_tag;
    env.payload.input_event.value.int_value = value;
    return env;
}

Envelope makeInputEventString(uint32_t elementId, uint32_t pageId, const char* text) {
    Envelope env{};
    env.which_payload = Envelope_input_event_tag;
    env.payload.input_event.element_id = elementId;
    env.payload.input_event.page_id = pageId;
    env.payload.input_event.which_value = InputEvent_string_value_tag;
    if (text != nullptr) {
        strncpy(env.payload.input_event.value.string_value, text, sizeof(env.payload.input_event.value.string_value) - 1);
        env.payload.input_event.value.string_value[sizeof(env.payload.input_event.value.string_value) - 1] = '\0';
    }
    return env;
}

void test_screen_client_incoming_show_page_reaches_ui() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    Envelope env{};
    env.which_payload = Envelope_show_page_tag;
    env.payload.show_page.page_id = 7;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, env, 11));

    client.tick();

    TEST_ASSERT_EQUAL_INT(1, adapter.showPageCount);
    TEST_ASSERT_EQUAL_UINT32(7, adapter.lastPageId);
}

void test_screen_client_incoming_commands_reach_ui() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    Envelope setText{};
    setText.which_payload = Envelope_set_text_tag;
    setText.payload.set_text.element_id = 10;
    strncpy(setText.payload.set_text.text, "hello", sizeof(setText.payload.set_text.text) - 1);
    setText.payload.set_text.text[sizeof(setText.payload.set_text.text) - 1] = '\0';
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setText, 1));

    Envelope setValue{};
    setValue.which_payload = Envelope_set_value_tag;
    setValue.payload.set_value.element_id = 20;
    setValue.payload.set_value.value = 321;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setValue, 2));

    Envelope setVisible{};
    setVisible.which_payload = Envelope_set_visible_tag;
    setVisible.payload.set_visible.element_id = 30;
    setVisible.payload.set_visible.visible = true;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setVisible, 3));

    Envelope setColor{};
    setColor.which_payload = Envelope_set_color_tag;
    setColor.payload.set_color.element_id = 40;
    setColor.payload.set_color.bg_color = 0x1234;
    setColor.payload.set_color.fg_color = 0x5678;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setColor, 4));

    Envelope setBatch{};
    setBatch.which_payload = Envelope_set_batch_tag;
    setBatch.payload.set_batch.texts_count = 1;
    setBatch.payload.set_batch.texts[0].element_id = 100;
    strncpy(setBatch.payload.set_batch.texts[0].text, "batch", sizeof(setBatch.payload.set_batch.texts[0].text) - 1);
    setBatch.payload.set_batch.texts[0].text[sizeof(setBatch.payload.set_batch.texts[0].text) - 1] = '\0';
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setBatch, 5));

    Envelope setElementAttribute{};
    setElementAttribute.which_payload = Envelope_set_element_attribute_tag;
    setElementAttribute.payload.set_element_attribute.element_id = 70;
    setElementAttribute.payload.set_element_attribute.attribute =
        ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH;
    setElementAttribute.payload.set_element_attribute.which_value =
        SetElementAttribute_int_value_tag;
    setElementAttribute.payload.set_element_attribute.value.int_value = 3;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, setElementAttribute, 6));

    client.tick();

    TEST_ASSERT_EQUAL_INT(1, adapter.setTextCount);
    TEST_ASSERT_EQUAL_UINT32(10, adapter.lastTextElementId);
    TEST_ASSERT_EQUAL_STRING("hello", adapter.lastText);

    TEST_ASSERT_EQUAL_INT(1, adapter.setValueCount);
    TEST_ASSERT_EQUAL_UINT32(20, adapter.lastValueElementId);
    TEST_ASSERT_EQUAL_INT32(321, adapter.lastValue);

    TEST_ASSERT_EQUAL_INT(1, adapter.setVisibleCount);
    TEST_ASSERT_EQUAL_UINT32(30, adapter.lastVisibleElementId);
    TEST_ASSERT_TRUE(adapter.lastVisible);

    TEST_ASSERT_EQUAL_INT(1, adapter.setColorCount);
    TEST_ASSERT_EQUAL_UINT32(40, adapter.lastColorElementId);
    TEST_ASSERT_EQUAL_UINT32(0x1234, adapter.lastBgColor);
    TEST_ASSERT_EQUAL_UINT32(0x5678, adapter.lastFgColor);

    TEST_ASSERT_EQUAL_INT(1, adapter.applyBatchCount);
    TEST_ASSERT_EQUAL_UINT8(1, adapter.lastBatch.texts_count);
    TEST_ASSERT_EQUAL_UINT32(100, adapter.lastBatch.texts[0].element_id);
    TEST_ASSERT_EQUAL_STRING("batch", adapter.lastBatch.texts[0].text);

    TEST_ASSERT_EQUAL_INT(1, adapter.setElementAttributeCount);
    TEST_ASSERT_EQUAL_UINT32(70, adapter.lastElementAttribute.element_id);
    TEST_ASSERT_EQUAL_INT(ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH, adapter.lastElementAttribute.attribute);
    TEST_ASSERT_EQUAL_UINT32(SetElementAttribute_int_value_tag, adapter.lastElementAttribute.which_value);
    TEST_ASSERT_EQUAL_INT32(3, adapter.lastElementAttribute.value.int_value);
}

void test_screen_client_tick_without_ui_adapter_is_safe() {
    MockTransport transport;
    screenlib::client::ScreenClient client(transport);
    client.init();

    Envelope env{};
    env.which_payload = Envelope_show_page_tag;
    env.payload.show_page.page_id = 3;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, env, 17));

    client.tick();

    TEST_ASSERT_TRUE(client.connected());
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(transport.tx.size()));
}

void test_screen_client_outgoing_button_event_from_adapter() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    adapter.queueEvent(makeButtonEvent(55, 4));
    client.tick();

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(55, out[0].payload.button_event.element_id);
    TEST_ASSERT_EQUAL_UINT32(4, out[0].payload.button_event.page_id);
    TEST_ASSERT_EQUAL_INT(ButtonAction_CLICK, out[0].payload.button_event.action);
}

void test_screen_client_outgoing_button_event_with_action_from_adapter() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    adapter.queueEvent(makeButtonEvent(91, 6, ButtonAction_PUSH));
    client.tick();

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(91, out[0].payload.button_event.element_id);
    TEST_ASSERT_EQUAL_UINT32(6, out[0].payload.button_event.page_id);
    TEST_ASSERT_EQUAL_INT(ButtonAction_PUSH, out[0].payload.button_event.action);
}

void test_screen_client_outgoing_input_events_from_adapter() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    adapter.queueEvent(makeInputEventInt(61, 8, 1234));
    adapter.queueEvent(makeInputEventString(62, 8, "abc"));
    client.tick();

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_input_event_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(61, out[0].payload.input_event.element_id);
    TEST_ASSERT_EQUAL_UINT32(8, out[0].payload.input_event.page_id);
    TEST_ASSERT_EQUAL_UINT32(InputEvent_int_value_tag, out[0].payload.input_event.which_value);
    TEST_ASSERT_EQUAL_INT32(1234, out[0].payload.input_event.value.int_value);

    TEST_ASSERT_EQUAL_UINT32(Envelope_input_event_tag, out[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(62, out[1].payload.input_event.element_id);
    TEST_ASSERT_EQUAL_UINT32(8, out[1].payload.input_event.page_id);
    TEST_ASSERT_EQUAL_UINT32(InputEvent_string_value_tag, out[1].payload.input_event.which_value);
    TEST_ASSERT_EQUAL_STRING("abc", out[1].payload.input_event.value.string_value);
}

void test_screen_client_set_ui_adapter_rebinds_sink() {
    MockTransport transport;
    FakeUiAdapter adapterA;
    FakeUiAdapter adapterB;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapterA);
    client.init();
    TEST_ASSERT_NOT_NULL(adapterA.currentSink());

    client.setUiAdapter(&adapterB);
    TEST_ASSERT_NULL(adapterA.currentSink());
    TEST_ASSERT_NOT_NULL(adapterB.currentSink());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, adapterA.clearEventSinkCalls);

    // Старый адаптер уже не должен публиковать события в runtime.
    TEST_ASSERT_FALSE(adapterA.emitNow(makeButtonEvent(1, 1)));
    TEST_ASSERT_TRUE(adapterB.emitNow(makeButtonEvent(2, 2)));

    client.tick();
    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));
    TEST_ASSERT_EQUAL_UINT32(2, out[0].payload.button_event.element_id);
}

void test_screen_client_ui_event_queue_overflow_is_safe() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    // kUiEventQueueSize = 8, добавляем больше.
    for (uint32_t i = 0; i < 10; ++i) {
        adapter.queueEvent(makeButtonEvent(100 + i, 1));
    }

    client.tick();

    TEST_ASSERT_EQUAL_UINT32(10u, static_cast<uint32_t>(adapter.emitResults.size()));
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_TRUE(adapter.emitResults[i]);
    }
    TEST_ASSERT_FALSE(adapter.emitResults[8]);
    TEST_ASSERT_FALSE(adapter.emitResults[9]);

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(8u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    // Runtime после overflow продолжает работать.
    adapter.queueEvent(makeButtonEvent(200, 1));
    client.tick();
    TEST_ASSERT_EQUAL_UINT32(9u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));
}

void test_screen_client_rejects_non_event_outbound_envelope_from_adapter() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    Envelope wrong{};
    wrong.which_payload = Envelope_show_page_tag;
    wrong.payload.show_page.page_id = 999;
    adapter.queueEvent(wrong);

    client.tick();

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));
}

void test_screen_client_incoming_non_screen_envelope_does_not_touch_ui() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();

    Envelope env{};
    env.which_payload = Envelope_heartbeat_tag;
    env.payload.heartbeat.uptime_ms = 123;
    TEST_ASSERT_TRUE(pushIncomingEnvelope(transport, env, 71));

    client.tick();

    TEST_ASSERT_EQUAL_INT(0, adapter.showPageCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.setTextCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.setValueCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.setVisibleCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.setColorCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.setElementAttributeCount);
    TEST_ASSERT_EQUAL_INT(0, adapter.applyBatchCount);
}

void test_screen_client_init_is_idempotent() {
    MockTransport transport;
    FakeUiAdapter adapter;
    screenlib::client::ScreenClient client(transport);

    client.setUiAdapter(&adapter);
    client.init();
    client.init();

    // При идемпотентном init привязка sink происходит один раз.
    TEST_ASSERT_EQUAL_INT(1, adapter.setEventSinkCalls);
    TEST_ASSERT_NOT_NULL(adapter.currentSink());
}

void test_screen_client_service_helpers_send_responses() {
    MockTransport transport;
    screenlib::client::ScreenClient client(transport);
    client.init();

    DeviceInfo info = DeviceInfo_init_zero;
    info.protocol_version = 1;
    strncpy(info.fw_version, "fw-1", sizeof(info.fw_version) - 1);
    info.fw_version[sizeof(info.fw_version) - 1] = '\0';
    strncpy(info.ui_version, "ui-1", sizeof(info.ui_version) - 1);
    info.ui_version[sizeof(info.ui_version) - 1] = '\0';

    TEST_ASSERT_TRUE(client.sendHello(info));
    TEST_ASSERT_TRUE(client.sendCurrentPage(12, 401));

    PageState pageState = PageState_init_zero;
    pageState.request_id = 402;
    pageState.page_id = 12;
    pageState.elements_count = 1;
    pageState.elements[0].element_id = 500;
    pageState.elements[0].type = ElementStateType_ELEMENT_STATE_INT;
    pageState.elements[0].which_value = PageElementState_int_value_tag;
    pageState.elements[0].value.int_value = 77;
    TEST_ASSERT_TRUE(client.sendPageState(pageState));

    ElementState elementState = ElementState_init_zero;
    elementState.request_id = 403;
    elementState.page_id = 12;
    elementState.found = true;
    elementState.has_element = true;
    elementState.element = pageState.elements[0];
    TEST_ASSERT_TRUE(client.sendElementState(elementState));

    ElementAttributeState elementAttributeState = ElementAttributeState_init_zero;
    elementAttributeState.request_id = 404;
    elementAttributeState.page_id = 12;
    elementAttributeState.element_id = 500;
    elementAttributeState.attribute = ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH;
    elementAttributeState.found = true;
    elementAttributeState.which_value = ElementAttributeState_int_value_tag;
    elementAttributeState.value.int_value = 123;
    TEST_ASSERT_TRUE(client.sendElementAttributeState(elementAttributeState));

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(5u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_hello_tag, out[0].which_payload);
    TEST_ASSERT_TRUE(out[0].payload.hello.has_device_info);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].payload.hello.device_info.protocol_version);
    TEST_ASSERT_EQUAL_STRING("fw-1", out[0].payload.hello.device_info.fw_version);

    TEST_ASSERT_EQUAL_UINT32(Envelope_current_page_tag, out[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(401, out[1].payload.current_page.request_id);
    TEST_ASSERT_EQUAL_UINT32(12, out[1].payload.current_page.page_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_page_state_tag, out[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(402, out[2].payload.page_state.request_id);
    TEST_ASSERT_EQUAL_UINT32(12, out[2].payload.page_state.page_id);
    TEST_ASSERT_EQUAL_UINT8(1, out[2].payload.page_state.elements_count);
    TEST_ASSERT_EQUAL_UINT32(500, out[2].payload.page_state.elements[0].element_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_element_state_tag, out[3].which_payload);
    TEST_ASSERT_EQUAL_UINT32(403, out[3].payload.element_state.request_id);
    TEST_ASSERT_TRUE(out[3].payload.element_state.has_element);

    TEST_ASSERT_EQUAL_UINT32(Envelope_element_attribute_state_tag, out[4].which_payload);
    TEST_ASSERT_EQUAL_UINT32(404, out[4].payload.element_attribute_state.request_id);
    TEST_ASSERT_EQUAL_UINT32(12, out[4].payload.element_attribute_state.page_id);
    TEST_ASSERT_EQUAL_UINT32(500, out[4].payload.element_attribute_state.element_id);
    TEST_ASSERT_TRUE(out[4].payload.element_attribute_state.found);
    TEST_ASSERT_EQUAL_UINT32(ElementAttributeState_int_value_tag, out[4].payload.element_attribute_state.which_value);
    TEST_ASSERT_EQUAL_INT32(123, out[4].payload.element_attribute_state.value.int_value);
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_screen_client_incoming_show_page_reaches_ui);
    RUN_TEST(test_screen_client_incoming_commands_reach_ui);
    RUN_TEST(test_screen_client_tick_without_ui_adapter_is_safe);
    RUN_TEST(test_screen_client_outgoing_button_event_from_adapter);
    RUN_TEST(test_screen_client_outgoing_button_event_with_action_from_adapter);
    RUN_TEST(test_screen_client_outgoing_input_events_from_adapter);
    RUN_TEST(test_screen_client_set_ui_adapter_rebinds_sink);
    RUN_TEST(test_screen_client_ui_event_queue_overflow_is_safe);
    RUN_TEST(test_screen_client_rejects_non_event_outbound_envelope_from_adapter);
    RUN_TEST(test_screen_client_incoming_non_screen_envelope_does_not_touch_ui);
    RUN_TEST(test_screen_client_init_is_idempotent);
    RUN_TEST(test_screen_client_service_helpers_send_responses);
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
