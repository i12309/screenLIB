#include <deque>
#include <string.h>
#include <vector>

#include <unity.h>

#include "config/ScreenConfig.h"
#include "frame/FrameCodec.h"
#include "manager/ScreenManager.h"
#include "proto/ProtoCodec.h"
#include "bridge/ScreenBridge.h"
#include "system/ScreenSystem.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// Простой mock транспорта для unit-тестов.
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

    void clearTx() { tx.clear(); }
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

    SetElementAttributeBatch batch = SetElementAttributeBatch_init_zero;
    batch.attributes_count = 1;
    batch.attributes[0].element_id = 42;
    batch.attributes[0].attribute = ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_COLOR;
    batch.attributes[0].which_value = SetElementAttribute_color_value_tag;
    batch.attributes[0].value.color_value = 0x00112233u;
    TEST_ASSERT_TRUE(bridge.setElementAttributeBatch(batch));

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
    TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(decodeAllTxEnvelopes(transport, out)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(41, out[0].payload.set_element_attribute.element_id);
    TEST_ASSERT_EQUAL_UINT32(120, static_cast<uint32_t>(out[0].payload.set_element_attribute.value.int_value));

    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_batch_tag, out[1].which_payload);
    TEST_ASSERT_EQUAL_UINT8(1, out[1].payload.set_element_attribute_batch.attributes_count);
    TEST_ASSERT_EQUAL_UINT32(42, out[1].payload.set_element_attribute_batch.attributes[0].element_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_element_attribute_tag, out[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(701, out[2].payload.request_element_attribute.request_id);
    TEST_ASSERT_EQUAL_UINT32(7, out[2].payload.request_element_attribute.page_id);
    TEST_ASSERT_EQUAL_UINT32(43, out[2].payload.request_element_attribute.element_id);

    TEST_ASSERT_EQUAL_UINT32(Envelope_element_attribute_state_tag, out[3].which_payload);
    TEST_ASSERT_EQUAL_UINT32(702, out[3].payload.element_attribute_state.request_id);
    TEST_ASSERT_TRUE(out[3].payload.element_attribute_state.found);
}

void test_screen_manager_routes_by_mode() {
    MockTransport physicalTransport;
    MockTransport webTransport;
    ScreenBridge physicalBridge(physicalTransport);
    ScreenBridge webBridge(webTransport);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.web.enabled = true;
    cfg.mirrorMode = screenlib::MirrorMode::PhysicalOnly;

    screenlib::ScreenManager manager;
    TEST_ASSERT_TRUE(manager.init(cfg));
    manager.bindPhysical(&physicalBridge);
    manager.bindWeb(&webBridge);

    TEST_ASSERT_TRUE(manager.showPage(3));
    TEST_ASSERT_GREATER_THAN(0u, physicalTransport.tx.size());
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(webTransport.tx.size()));

    physicalTransport.clearTx();
    webTransport.clearTx();
    cfg.mirrorMode = screenlib::MirrorMode::Both;
    TEST_ASSERT_TRUE(manager.init(cfg));

    TEST_ASSERT_TRUE(manager.setText(10, "both"));
    TEST_ASSERT_GREATER_THAN(0u, physicalTransport.tx.size());
    TEST_ASSERT_GREATER_THAN(0u, webTransport.tx.size());
}

struct CapturedEvent {
    int count = 0;
    screenlib::ScreenEventContext ctx{};
    pb_size_t tag = 0;
} gCaptured;

void onManagerEvent(const Envelope& env, const screenlib::ScreenEventContext& ctx, void* userData) {
    (void)userData;
    gCaptured.count++;
    gCaptured.ctx = ctx;
    gCaptured.tag = env.which_payload;
}

void test_screen_manager_receives_incoming_events_with_context() {
    MockTransport physicalTransport;
    MockTransport webTransport;
    ScreenBridge physicalBridge(physicalTransport);
    ScreenBridge webBridge(webTransport);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.web.enabled = true;
    cfg.mirrorMode = screenlib::MirrorMode::Both;

    screenlib::ScreenManager manager;
    TEST_ASSERT_TRUE(manager.init(cfg));
    manager.bindPhysical(&physicalBridge);
    manager.bindWeb(&webBridge);
    manager.setEventHandler(&onManagerEvent, nullptr);

    gCaptured = CapturedEvent{};

    Envelope eventEnv{};
    eventEnv.which_payload = Envelope_button_event_tag;
    eventEnv.payload.button_event.page_id = 2;
    eventEnv.payload.button_event.element_id = 55;

    std::vector<uint8_t> frame;
    TEST_ASSERT_TRUE(buildFrameFromEnvelope(eventEnv, frame, 5));
    physicalTransport.pushRx(frame.data(), frame.size());

    manager.tick();

    TEST_ASSERT_EQUAL_INT(1, gCaptured.count);
    TEST_ASSERT_TRUE(gCaptured.ctx.isPhysical);
    TEST_ASSERT_FALSE(gCaptured.ctx.isWeb);
    TEST_ASSERT_EQUAL_UINT8(0, gCaptured.ctx.endpointId);
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, gCaptured.tag);
}

void test_screen_system_rejects_web_ws_client_on_host_side() {
    screenlib::ScreenSystem screens;

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = false;
    cfg.web.enabled = true;
    cfg.web.type = screenlib::OutputType::WsClient;
    strncpy(cfg.web.wsClient.url, "ws://127.0.0.1:8181/ws", sizeof(cfg.web.wsClient.url) - 1);
    cfg.web.wsClient.url[sizeof(cfg.web.wsClient.url) - 1] = '\0';

    TEST_ASSERT_FALSE(screens.init(cfg));
    TEST_ASSERT_EQUAL_STRING("ws_client is client-side transport", screens.lastError());
}

void test_screen_system_mirror_mode_sends_to_both_endpoints() {
    MockTransport physicalTransport;
    MockTransport webTransport;
    ScreenBridge physicalBridge(physicalTransport);
    ScreenBridge webBridge(webTransport);

    screenlib::ScreenSystem screens;
    screens.bindPhysicalBridge(&physicalBridge);
    screens.bindWebBridge(&webBridge);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.physical.type = screenlib::OutputType::WsServer;
    cfg.physical.wsServer.port = 1881;
    cfg.web.enabled = true;
    cfg.web.type = screenlib::OutputType::WsServer;
    cfg.web.wsServer.port = 1882;
    cfg.mirrorMode = screenlib::MirrorMode::Both;

    TEST_ASSERT_TRUE(screens.init(cfg));

    TEST_ASSERT_TRUE(screens.showPage(11));
    TEST_ASSERT_TRUE(screens.setText(51, "mirror"));

    TEST_ASSERT_GREATER_THAN(0u, physicalTransport.tx.size());
    TEST_ASSERT_GREATER_THAN(0u, webTransport.tx.size());
}

void test_screen_system_service_requests_route_to_both_endpoints() {
    MockTransport physicalTransport;
    MockTransport webTransport;
    ScreenBridge physicalBridge(physicalTransport);
    ScreenBridge webBridge(webTransport);

    screenlib::ScreenSystem screens;
    screens.bindPhysicalBridge(&physicalBridge);
    screens.bindWebBridge(&webBridge);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.physical.type = screenlib::OutputType::WsServer;
    cfg.physical.wsServer.port = 1991;
    cfg.web.enabled = true;
    cfg.web.type = screenlib::OutputType::WsServer;
    cfg.web.wsServer.port = 1992;
    cfg.mirrorMode = screenlib::MirrorMode::Both;

    TEST_ASSERT_TRUE(screens.init(cfg));

    TEST_ASSERT_TRUE(screens.requestDeviceInfo(301));
    TEST_ASSERT_TRUE(screens.requestCurrentPage(302));
    TEST_ASSERT_TRUE(screens.requestPageState(4, 303));

    std::vector<Envelope> physicalOut;
    std::vector<Envelope> webOut;
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(decodeAllTxEnvelopes(physicalTransport, physicalOut)));
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(decodeAllTxEnvelopes(webTransport, webOut)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_request_device_info_tag, physicalOut[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(Envelope_request_current_page_tag, physicalOut[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(Envelope_request_page_state_tag, physicalOut[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(303, physicalOut[2].payload.request_page_state.request_id);
    TEST_ASSERT_EQUAL_UINT32(4, physicalOut[2].payload.request_page_state.page_id);
}

void test_screen_system_typed_attribute_helpers_route_to_both_endpoints() {
    MockTransport physicalTransport;
    MockTransport webTransport;
    ScreenBridge physicalBridge(physicalTransport);
    ScreenBridge webBridge(webTransport);

    screenlib::ScreenSystem screens;
    screens.bindPhysicalBridge(&physicalBridge);
    screens.bindWebBridge(&webBridge);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.physical.type = screenlib::OutputType::WsServer;
    cfg.physical.wsServer.port = 2991;
    cfg.web.enabled = true;
    cfg.web.type = screenlib::OutputType::WsServer;
    cfg.web.wsServer.port = 2992;
    cfg.mirrorMode = screenlib::MirrorMode::Both;

    TEST_ASSERT_TRUE(screens.init(cfg));

    TEST_ASSERT_TRUE(screens.setElementWidth(110, 320));
    TEST_ASSERT_TRUE(screens.setElementTextColor(111, 0x00AABBCCu));
    TEST_ASSERT_TRUE(screens.setElementTextFont(111, ElementFont_ELEMENT_FONT_UI_M20));
    TEST_ASSERT_TRUE(screens.requestElementTextFont(111, 9, 901));

    std::vector<Envelope> physicalOut;
    std::vector<Envelope> webOut;
    TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(decodeAllTxEnvelopes(physicalTransport, physicalOut)));
    TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(decodeAllTxEnvelopes(webTransport, webOut)));

    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, physicalOut[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH,
                             physicalOut[0].payload.set_element_attribute.attribute);
    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, physicalOut[1].which_payload);
    TEST_ASSERT_EQUAL_UINT32(ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_COLOR,
                             physicalOut[1].payload.set_element_attribute.attribute);
    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, physicalOut[2].which_payload);
    TEST_ASSERT_EQUAL_UINT32(ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT,
                             physicalOut[2].payload.set_element_attribute.attribute);
    TEST_ASSERT_EQUAL_UINT32(Envelope_request_element_attribute_tag, physicalOut[3].which_payload);
    TEST_ASSERT_EQUAL_UINT32(901, physicalOut[3].payload.request_element_attribute.request_id);
    TEST_ASSERT_EQUAL_UINT32(9, physicalOut[3].payload.request_element_attribute.page_id);
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
    RUN_TEST(test_screen_manager_routes_by_mode);
    RUN_TEST(test_screen_manager_receives_incoming_events_with_context);
    RUN_TEST(test_screen_system_rejects_web_ws_client_on_host_side);
    RUN_TEST(test_screen_system_mirror_mode_sends_to_both_endpoints);
    RUN_TEST(test_screen_system_service_requests_route_to_both_endpoints);
    RUN_TEST(test_screen_system_typed_attribute_helpers_route_to_both_endpoints);
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
