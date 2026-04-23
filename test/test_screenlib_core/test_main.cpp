#include <deque>
#include <string.h>
#include <vector>

#include <unity.h>

#include "bridge/ScreenBridge.h"
#include "frame/FrameCodec.h"
#include "proto/ProtoCodec.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// Простой mock транспорта для unit-тестов ScreenBridge.
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

bool decodeFirstTxEnvelope(const MockTransport& transport, Envelope& out) {
    if (transport.tx.empty()) return false;

    FrameCodec codec;
    codec.feed(transport.tx.data(), transport.tx.size());

    FrameCodec::Frame frame;
    if (!codec.popFrame(frame)) return false;

    return ProtoCodec::decode(frame.payload, frame.payloadLen, out);
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

bool buildFrameFromEnvelope(const Envelope& env, std::vector<uint8_t>& outFrame, uint8_t seq = 1) {
    uint8_t proto[ProtoCodec::kMaxEncodedSize] = {};
    const size_t protoLen = ProtoCodec::encode(env, proto, sizeof(proto));
    if (protoLen == 0) return false;

    uint8_t frame[kMaxPayload + kFrameOverhead] = {};
    const size_t frameLen = FrameCodec::pack(
        proto,
        static_cast<uint16_t>(protoLen),
        frame,
        sizeof(frame),
        seq,
        kFrameVer
    );
    if (frameLen == 0) return false;

    outFrame.assign(frame, frame + frameLen);
    return true;
}

void test_screen_bridge_show_page_encodes_envelope() {
    MockTransport transport;
    ScreenBridge bridge(transport);

    TEST_ASSERT_TRUE(bridge.showPage(7));
    TEST_ASSERT_GREATER_THAN(0u, transport.tx.size());

    Envelope decoded{};
    TEST_ASSERT_TRUE(decodeFirstTxEnvelope(transport, decoded));
    TEST_ASSERT_EQUAL_UINT32(Envelope_show_page_tag, decoded.which_payload);
    TEST_ASSERT_EQUAL_UINT32(7, decoded.payload.show_page.page_id);
}

void test_screen_bridge_service_helpers_encode_envelopes() {
    MockTransport transport;
    ScreenBridge bridge(transport);

    TEST_ASSERT_TRUE(bridge.requestDeviceInfo(101));
    TEST_ASSERT_TRUE(bridge.requestCurrentPage(102));
    TEST_ASSERT_TRUE(bridge.requestPageState(7, 103));
    TEST_ASSERT_TRUE(bridge.requestElementState(88, 7, 104));

    CurrentPage currentPage = CurrentPage_init_zero;
    currentPage.request_id = 105;
    currentPage.page_id = 9;
    TEST_ASSERT_TRUE(bridge.sendCurrentPage(currentPage.page_id, currentPage.request_id));

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(5u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_device_info_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(101, out[0].payload.request_device_info.request_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_current_page_tag, out[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(102, out[1].payload.request_current_page.request_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_page_state_tag, out[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(103, out[2].payload.request_page_state.request_id);
    TEST_ASSERT_EQUAL_UINT32(7, out[2].payload.request_page_state.page_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_element_state_tag, out[3].which_payload);
    TEST_ASSERT_EQUAL_UINT32(104, out[3].payload.request_element_state.request_id);
    TEST_ASSERT_EQUAL_UINT32(7, out[3].payload.request_element_state.page_id);
    TEST_ASSERT_EQUAL_UINT32(88, out[3].payload.request_element_state.element_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_current_page_tag, out[4].which_payload);
    TEST_ASSERT_EQUAL_UINT32(105, out[4].payload.current_page.request_id);
    TEST_ASSERT_EQUAL_UINT32(9, out[4].payload.current_page.page_id);
}

void test_screen_bridge_attribute_helpers_encode_envelopes() {
    MockTransport transport;
    ScreenBridge bridge(transport);

    SetElementAttribute setAttr = SetElementAttribute_init_zero;
    setAttr.element_id = 41;
    setAttr.attribute = ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH;
    setAttr.which_value = SetElementAttribute_int_value_tag;
    setAttr.value.int_value = 120;
    TEST_ASSERT_TRUE(bridge.setElementAttribute(setAttr));

    TEST_ASSERT_TRUE(bridge.requestElementAttribute(
        43, ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT, 7, 701));

    ElementAttributeState state = ElementAttributeState_init_zero;
    state.request_id = 702;
    state.page_id = 7;
    state.element_id = 43;
    state.attribute = ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT;
    state.found = true;
    state.which_value = ElementAttributeState_font_value_tag;
    state.value.font_value = ElementFont_ELEMENT_FONT_UI_M24;
    TEST_ASSERT_TRUE(bridge.sendElementAttributeState(state));

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(41, out[0].payload.set_element_attribute.element_id);
    TEST_ASSERT_EQUAL_UINT32(120, static_cast<uint32_t>(out[0].payload.set_element_attribute.value.int_value));

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_element_attribute_tag, out[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(701, out[1].payload.request_element_attribute.request_id);
    TEST_ASSERT_EQUAL_UINT32(7, out[1].payload.request_element_attribute.page_id);
    TEST_ASSERT_EQUAL_UINT32(43, out[1].payload.request_element_attribute.element_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_element_attribute_state_tag, out[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(702, out[2].payload.element_attribute_state.request_id);
    TEST_ASSERT_TRUE(out[2].payload.element_attribute_state.found);
}

struct BridgeCapture {
    int count = 0;
} gBridgeCapture;

void onBridgeEvent(const Envelope& env, void* userData) {
    (void)env;
    BridgeCapture* cap = static_cast<BridgeCapture*>(userData);
    if (cap != nullptr) {
        cap->count++;
    }
}

void test_screen_bridge_resets_parser_on_disconnect_reconnect() {
    MockTransport transport;
    ScreenBridge bridge(transport);
    gBridgeCapture = BridgeCapture{};
    bridge.setEnvelopeHandler(&onBridgeEvent, &gBridgeCapture);

    Envelope env{};
    env.which_payload = Envelope_show_page_tag;
    env.payload.show_page.page_id = 9;

    std::vector<uint8_t> fullFrame;
    TEST_ASSERT_TRUE(buildFrameFromEnvelope(env, fullFrame, 9));
    TEST_ASSERT_GREATER_THAN(4u, fullFrame.size());

    // 1) подаём только часть кадра.
    transport.pushRx(fullFrame.data(), fullFrame.size() / 2);
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(bridge.poll()));
    TEST_ASSERT_EQUAL_INT(0, gBridgeCapture.count);

    // 2) имитируем разрыв канала: bridge должен сбросить parser.
    transport.isConnected = false;
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(bridge.poll()));

    // 3) после reconnect полный кадр должен обработаться корректно.
    transport.isConnected = true;
    transport.pushRx(fullFrame.data(), fullFrame.size());
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(bridge.poll()));
    TEST_ASSERT_EQUAL_INT(1, gBridgeCapture.count);
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_screen_bridge_show_page_encodes_envelope);
    RUN_TEST(test_screen_bridge_service_helpers_encode_envelopes);
    RUN_TEST(test_screen_bridge_attribute_helpers_encode_envelopes);
    RUN_TEST(test_screen_bridge_resets_parser_on_disconnect_reconnect);
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
