#include <deque>
#include <string.h>
#include <vector>

#include <unity.h>

#include "config/ScreenConfig.h"
#include "frame/FrameCodec.h"
#include "manager/ScreenManager.h"
#include "pages/PageRegistry.h"
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

class TestPageController : public screenlib::IPageController {
public:
    explicit TestPageController(uint32_t id) : _id(id) {}

    uint32_t pageId() const override { return _id; }

    void onEnter() override { enterCount++; }
    void onLeave() override { leaveCount++; }

    bool onEnvelope(const Envelope& env, const screenlib::ScreenEventContext& ctx) override {
        envelopeCount++;
        lastTag = env.which_payload;
        lastCtx = ctx;
        return true;
    }

    int enterCount = 0;
    int leaveCount = 0;
    int envelopeCount = 0;
    pb_size_t lastTag = 0;
    screenlib::ScreenEventContext lastCtx{};

private:
    uint32_t _id = 0;
};

void test_page_registry_switch_calls_enter_leave() {
    screenlib::PageRegistry registry;
    TestPageController page1(1);
    TestPageController page2(2);

    TEST_ASSERT_TRUE(registry.registerPage(&page1));
    TEST_ASSERT_TRUE(registry.registerPage(&page2));
    TEST_ASSERT_EQUAL_UINT32(0, registry.currentPage());

    TEST_ASSERT_TRUE(registry.setCurrentPage(1));
    TEST_ASSERT_EQUAL_UINT32(1, registry.currentPage());
    TEST_ASSERT_EQUAL_INT(1, page1.enterCount);
    TEST_ASSERT_EQUAL_INT(0, page1.leaveCount);

    TEST_ASSERT_TRUE(registry.setCurrentPage(2));
    TEST_ASSERT_EQUAL_UINT32(2, registry.currentPage());
    TEST_ASSERT_EQUAL_INT(1, page1.leaveCount);
    TEST_ASSERT_EQUAL_INT(1, page2.enterCount);

    // Повторный выбор той же страницы не должен вызывать лишние enter/leave.
    TEST_ASSERT_TRUE(registry.setCurrentPage(2));
    TEST_ASSERT_EQUAL_INT(1, page2.enterCount);
    TEST_ASSERT_EQUAL_INT(0, page2.leaveCount);
}

void test_screen_manager_dispatches_ui_events_to_active_page_and_keeps_callback() {
    MockTransport physicalTransport;
    ScreenBridge physicalBridge(physicalTransport);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.web.enabled = false;
    cfg.mirrorMode = screenlib::MirrorMode::PhysicalOnly;

    screenlib::ScreenManager manager;
    TEST_ASSERT_TRUE(manager.init(cfg));
    manager.bindPhysical(&physicalBridge);

    screenlib::PageRegistry registry;
    TestPageController page1(1);
    TestPageController page2(2);
    TEST_ASSERT_TRUE(registry.registerPage(&page1));
    TEST_ASSERT_TRUE(registry.registerPage(&page2));
    TEST_ASSERT_TRUE(registry.setCurrentPage(1));

    manager.setPageRegistry(&registry);
    manager.setEventHandler(&onManagerEvent, nullptr);
    gCaptured = CapturedEvent{};

    Envelope buttonEvent{};
    buttonEvent.which_payload = Envelope_button_event_tag;
    buttonEvent.payload.button_event.page_id = 1;
    buttonEvent.payload.button_event.element_id = 77;

    std::vector<uint8_t> frame;
    TEST_ASSERT_TRUE(buildFrameFromEnvelope(buttonEvent, frame, 21));
    physicalTransport.pushRx(frame.data(), frame.size());
    manager.tick();

    TEST_ASSERT_EQUAL_INT(1, page1.envelopeCount);
    TEST_ASSERT_EQUAL_INT(0, page2.envelopeCount);
    TEST_ASSERT_EQUAL_UINT32(Envelope_button_event_tag, page1.lastTag);
    TEST_ASSERT_TRUE(page1.lastCtx.isPhysical);
    TEST_ASSERT_EQUAL_INT(1, gCaptured.count);

    TEST_ASSERT_TRUE(registry.setCurrentPage(2));

    Envelope inputEvent{};
    inputEvent.which_payload = Envelope_input_event_tag;
    inputEvent.payload.input_event.page_id = 2;
    inputEvent.payload.input_event.element_id = 99;
    inputEvent.payload.input_event.which_value = InputEvent_int_value_tag;
    inputEvent.payload.input_event.value.int_value = 123;

    frame.clear();
    TEST_ASSERT_TRUE(buildFrameFromEnvelope(inputEvent, frame, 22));
    physicalTransport.pushRx(frame.data(), frame.size());
    manager.tick();

    TEST_ASSERT_EQUAL_INT(1, page1.envelopeCount);
    TEST_ASSERT_EQUAL_INT(1, page2.envelopeCount);
    TEST_ASSERT_EQUAL_UINT32(Envelope_input_event_tag, page2.lastTag);
    TEST_ASSERT_EQUAL_INT(2, gCaptured.count);
}

void test_screen_manager_show_page_syncs_registry_and_routes_to_new_page() {
    MockTransport physicalTransport;
    ScreenBridge physicalBridge(physicalTransport);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.web.enabled = false;
    cfg.mirrorMode = screenlib::MirrorMode::PhysicalOnly;

    screenlib::ScreenManager manager;
    TEST_ASSERT_TRUE(manager.init(cfg));
    manager.bindPhysical(&physicalBridge);

    screenlib::PageRegistry registry;
    TestPageController page1(1);
    TestPageController page2(2);
    TEST_ASSERT_TRUE(registry.registerPage(&page1));
    TEST_ASSERT_TRUE(registry.registerPage(&page2));
    manager.setPageRegistry(&registry);

    // showPage должен автоматически синхронизировать текущую страницу в registry.
    TEST_ASSERT_TRUE(manager.showPage(1));
    TEST_ASSERT_EQUAL_UINT32(1, registry.currentPage());
    TEST_ASSERT_EQUAL_INT(1, page1.enterCount);
    TEST_ASSERT_EQUAL_INT(0, page2.enterCount);

    TEST_ASSERT_TRUE(manager.showPage(2));
    TEST_ASSERT_EQUAL_UINT32(2, registry.currentPage());
    TEST_ASSERT_EQUAL_INT(1, page1.leaveCount);
    TEST_ASSERT_EQUAL_INT(1, page2.enterCount);

    Envelope buttonEvent{};
    buttonEvent.which_payload = Envelope_button_event_tag;
    buttonEvent.payload.button_event.page_id = 2;
    buttonEvent.payload.button_event.element_id = 41;

    std::vector<uint8_t> frame;
    TEST_ASSERT_TRUE(buildFrameFromEnvelope(buttonEvent, frame, 31));
    physicalTransport.pushRx(frame.data(), frame.size());
    manager.tick();

    TEST_ASSERT_EQUAL_INT(0, page1.envelopeCount);
    TEST_ASSERT_EQUAL_INT(1, page2.envelopeCount);
}

void test_screen_manager_show_page_failure_does_not_advance_registry() {
    MockTransport physicalTransport;
    ScreenBridge physicalBridge(physicalTransport);

    screenlib::ScreenConfig cfg{};
    cfg.physical.enabled = true;
    cfg.web.enabled = false;
    cfg.mirrorMode = screenlib::MirrorMode::PhysicalOnly;

    screenlib::ScreenManager manager;
    TEST_ASSERT_TRUE(manager.init(cfg));
    manager.bindPhysical(&physicalBridge);

    screenlib::PageRegistry registry;
    TestPageController page1(1);
    TestPageController page2(2);
    TEST_ASSERT_TRUE(registry.registerPage(&page1));
    TEST_ASSERT_TRUE(registry.registerPage(&page2));
    manager.setPageRegistry(&registry);

    TEST_ASSERT_TRUE(manager.showPage(1));
    TEST_ASSERT_EQUAL_UINT32(1, registry.currentPage());

    // Эмулируем полный провал отправки showPage.
    physicalTransport.isConnected = false;
    TEST_ASSERT_FALSE(manager.showPage(2));
    TEST_ASSERT_EQUAL_UINT32(1, registry.currentPage());
    TEST_ASSERT_EQUAL_INT(1, page1.enterCount);
    TEST_ASSERT_EQUAL_INT(0, page1.leaveCount);
    TEST_ASSERT_EQUAL_INT(0, page2.enterCount);
}

void test_page_registry_strict_policy_ignores_foreign_page_id() {
    screenlib::PageRegistry registry;
    TestPageController page1(1);
    TestPageController page2(2);

    TEST_ASSERT_TRUE(registry.registerPage(&page1));
    TEST_ASSERT_TRUE(registry.registerPage(&page2));
    TEST_ASSERT_TRUE(registry.setCurrentPage(1));

    Envelope buttonEvent{};
    buttonEvent.which_payload = Envelope_button_event_tag;
    buttonEvent.payload.button_event.page_id = 2;
    buttonEvent.payload.button_event.element_id = 17;

    screenlib::ScreenEventContext ctx{};
    const bool handled = registry.dispatchEnvelope(buttonEvent, ctx);
    TEST_ASSERT_FALSE(handled);
    TEST_ASSERT_EQUAL_INT(0, page1.envelopeCount);
    TEST_ASSERT_EQUAL_INT(0, page2.envelopeCount);
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
    RUN_TEST(test_page_registry_switch_calls_enter_leave);
    RUN_TEST(test_screen_manager_dispatches_ui_events_to_active_page_and_keeps_callback);
    RUN_TEST(test_screen_manager_show_page_syncs_registry_and_routes_to_new_page);
    RUN_TEST(test_screen_manager_show_page_failure_does_not_advance_registry);
    RUN_TEST(test_page_registry_strict_policy_ignores_foreign_page_id);
    RUN_TEST(test_screen_system_rejects_web_ws_client_on_host_side);
    RUN_TEST(test_screen_system_mirror_mode_sends_to_both_endpoints);
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
