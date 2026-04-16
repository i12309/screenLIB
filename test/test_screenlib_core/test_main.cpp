#include <deque>
#include <vector>

#include <unity.h>

#include "config/ScreenConfig.h"
#include "frame/FrameCodec.h"
#include "manager/ScreenManager.h"
#include "proto/ProtoCodec.h"
#include "screen/ScreenBridge.h"

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
    RUN_TEST(test_screen_manager_routes_by_mode);
    RUN_TEST(test_screen_manager_receives_incoming_events_with_context);
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
