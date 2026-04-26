#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include <unity.h>

#include "bridge/ScreenBridge.h"
#include "chunk/TextChunkSender.h"
#include "config/ScreenConfig.h"
#include "frame/FrameCodec.h"
#include "proto/ProtoCodec.h"
#include "runtime/PageRuntime.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// ---------- Mock транспорт ----------

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
        for (size_t i = 0; i < len; ++i) rx.push_back(data[i]);
    }

    void clearTx() { tx.clear(); }
};

bool buildFrameFromEnvelope(const Envelope& env, std::vector<uint8_t>& out, uint8_t seq = 1) {
    uint8_t proto[ProtoCodec::kMaxEncodedSize] = {};
    const size_t protoLen = ProtoCodec::encode(env, proto, sizeof(proto));
    if (protoLen == 0) return false;

    uint8_t frame[kMaxPayload + kFrameOverhead] = {};
    const size_t frameLen = FrameCodec::pack(
        proto, static_cast<uint16_t>(protoLen), frame, sizeof(frame), seq, kFrameVer);
    if (frameLen == 0) return false;

    out.assign(frame, frame + frameLen);
    return true;
}

bool pushIncoming(MockTransport& transport, const Envelope& env, uint8_t seq = 1) {
    std::vector<uint8_t> frame;
    if (!buildFrameFromEnvelope(env, frame, seq)) return false;
    transport.pushRx(frame.data(), frame.size());
    return true;
}

size_t decodeTxEnvelopes(const MockTransport& transport, std::vector<Envelope>& out) {
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

bool collectEnvelope(const Envelope& env, void* userData) {
    auto* envelopes = static_cast<std::vector<Envelope>*>(userData);
    if (envelopes == nullptr) return false;
    envelopes->push_back(env);
    return true;
}

// ---------- Управляемое время ----------

uint32_t g_now = 0;
uint32_t fake_now() { return g_now; }

// ---------- Тестовая страница ----------

struct TestPageStats {
    int showCalls = 0;
    int closeCalls = 0;
    int tickCalls = 0;
    int buttonCalls = 0;
    uint32_t lastButtonElementId = 0;
    ButtonAction lastButtonAction = ButtonAction_CLICK;
} g_stats;

class TestPage : public screenlib::IPage {
public:
    static constexpr uint32_t kPageId = 100;
    uint32_t pageId() const override { return kPageId; }

protected:
    void onShow() override { g_stats.showCalls++; }
    void onClose() override { g_stats.closeCalls++; }
    void onTick() override { g_stats.tickCalls++; }
    void onButton(uint32_t elementId, ButtonAction action) override {
        g_stats.buttonCalls++;
        g_stats.lastButtonElementId = elementId;
        g_stats.lastButtonAction = action;
    }
};

class OtherPage : public screenlib::IPage {
public:
    static constexpr uint32_t kPageId = 200;
    uint32_t pageId() const override { return kPageId; }
};

// ---------- Link listener ----------

struct LinkCapture {
    int calls = 0;
    bool lastUp = true;
} g_link;

void onLinkChange(bool up, void* /*user*/) {
    g_link.calls++;
    g_link.lastUp = up;
}

// ---------- Helpers ----------

Envelope makePageSnapshotEnvelope(uint32_t pageId, uint32_t sessionId) {
    Envelope env{};
    env.which_payload = Envelope_page_snapshot_tag;
    env.payload.page_snapshot.page_id = pageId;
    env.payload.page_snapshot.session_id = sessionId;
    env.payload.page_snapshot.elements_count = 1;
    env.payload.page_snapshot.elements[0].element_id = 1;
    env.payload.page_snapshot.elements[0].attributes_count = 1;
    env.payload.page_snapshot.elements[0].attributes[0].attribute =
        ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH;
    env.payload.page_snapshot.elements[0].attributes[0].which_value =
        ElementAttributeValue_int_value_tag;
    env.payload.page_snapshot.elements[0].attributes[0].value.int_value = 320;
    return env;
}

Envelope makeTextPageSnapshotEnvelope(uint32_t pageId,
                                      uint32_t sessionId,
                                      uint32_t elementId,
                                      const char* text) {
    Envelope env{};
    env.which_payload = Envelope_page_snapshot_tag;
    env.payload.page_snapshot.page_id = pageId;
    env.payload.page_snapshot.session_id = sessionId;
    env.payload.page_snapshot.elements_count = 1;
    env.payload.page_snapshot.elements[0].element_id = elementId;
    env.payload.page_snapshot.elements[0].attributes_count = 1;
    env.payload.page_snapshot.elements[0].attributes[0].attribute =
        ElementAttribute_ELEMENT_ATTRIBUTE_TEXT;
    env.payload.page_snapshot.elements[0].attributes[0].which_value =
        ElementAttributeValue_string_value_tag;
    std::strncpy(env.payload.page_snapshot.elements[0].attributes[0].value.string_value,
                 text != nullptr ? text : "",
                 sizeof(env.payload.page_snapshot.elements[0].attributes[0].value.string_value) - 1);
    return env;
}

Envelope makeAttributeChangedAck(uint32_t pageId, uint32_t sessionId,
                                 uint32_t elementId, uint32_t requestId, int32_t v) {
    Envelope env{};
    env.which_payload = Envelope_attribute_changed_tag;
    env.payload.attribute_changed.page_id = pageId;
    env.payload.attribute_changed.session_id = sessionId;
    env.payload.attribute_changed.element_id = elementId;
    env.payload.attribute_changed.has_value = true;
    env.payload.attribute_changed.value.attribute =
        ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH;
    env.payload.attribute_changed.value.which_value = ElementAttributeValue_int_value_tag;
    env.payload.attribute_changed.value.value.int_value = v;
    env.payload.attribute_changed.reason = AttributeChangeReason_REASON_COMMAND_APPLIED;
    env.payload.attribute_changed.in_reply_to_request = requestId;
    return env;
}

Envelope makeButtonEventEnvelope(uint32_t pageId, uint32_t sessionId, uint32_t elementId,
                                 ButtonAction action = ButtonAction_CLICK) {
    Envelope env{};
    env.which_payload = Envelope_button_event_tag;
    env.payload.button_event.page_id = pageId;
    env.payload.button_event.session_id = sessionId;
    env.payload.button_event.element_id = elementId;
    env.payload.button_event.action = action;
    return env;
}

ElementAttributeValue makeIntValue(ElementAttribute a, int32_t v) {
    ElementAttributeValue eav = ElementAttributeValue_init_zero;
    eav.attribute = a;
    eav.which_value = ElementAttributeValue_int_value_tag;
    eav.value.int_value = v;
    return eav;
}

// ---------- Общая подготовка ----------

struct TestCtx {
    MockTransport transport;
    ScreenBridge bridge;
    screenlib::PageRuntime runtime;

    TestCtx() : bridge(transport) {
        g_now = 0;
        g_stats = {};
        g_link = {};

        screenlib::ScreenConfig cfg{};
        cfg.physical.enabled = true;
        cfg.mirrorMode = screenlib::MirrorMode::PhysicalOnly;
        runtime.init(cfg);
        runtime.attachPhysicalBridge(&bridge);
        runtime.setLinkListener(&onLinkChange, nullptr);
        runtime.setNowProvider(&fake_now);
    }
};

// ---------- Тесты ----------

void test_navigate_sends_showpage_with_new_session() {
    TestCtx ctx;

    TEST_ASSERT_TRUE(ctx.runtime.navigateTo<TestPage>());

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(decodeTxEnvelopes(ctx.transport, out)));
    TEST_ASSERT_EQUAL_UINT32(Envelope_show_page_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(TestPage::kPageId, out[0].payload.show_page.page_id);
    TEST_ASSERT_EQUAL_UINT32(1u, out[0].payload.show_page.session_id);
    TEST_ASSERT_EQUAL_UINT32(TestPage::kPageId, ctx.runtime.currentPageId());
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.runtime.currentSessionId());
}

void test_on_show_fires_when_snapshot_arrives() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.transport.clearTx();

    TEST_ASSERT_EQUAL_INT(0, g_stats.showCalls);

    Envelope snap = makePageSnapshotEnvelope(TestPage::kPageId, /*session=*/1);
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport, snap, 1));

    ctx.runtime.tick();

    TEST_ASSERT_EQUAL_INT(1, g_stats.showCalls);
    TEST_ASSERT_TRUE(ctx.runtime.pageSynced());
    TEST_ASSERT_EQUAL_INT32(320, ctx.runtime.model().getInt(
        1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));

    // повторный snapshot не должен снова дёрнуть onShow
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport,
                                  makePageSnapshotEnvelope(TestPage::kPageId, 1), 2));
    ctx.runtime.tick();
    TEST_ASSERT_EQUAL_INT(1, g_stats.showCalls);
}

void test_stale_snapshot_is_ignored() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();   // session=1

    // Snapshot от другой сессии.
    Envelope stale = makePageSnapshotEnvelope(TestPage::kPageId, /*session=*/999);
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport, stale, 1));
    ctx.runtime.tick();

    TEST_ASSERT_EQUAL_INT(0, g_stats.showCalls);
    TEST_ASSERT_FALSE(ctx.runtime.pageSynced());
}

void test_page_snapshot_with_long_text_field() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();

    constexpr uint32_t kElementId = 77;
    const char* longText =
        "This text is intentionally longer than the 48 byte ElementAttributeValue "
        "string field, so it must arrive after the snapshot as text chunks.";

    Envelope snap = makeTextPageSnapshotEnvelope(TestPage::kPageId, 1, kElementId, "");
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport, snap, 1));
    ctx.runtime.tick();

    TEST_ASSERT_TRUE(ctx.runtime.pageSynced());
    TEST_ASSERT_EQUAL_STRING("", ctx.runtime.model().getString(
                                     kElementId,
                                     ElementAttribute_ELEMENT_ATTRIBUTE_TEXT));

    std::vector<Envelope> chunks;
    TEST_ASSERT_TRUE(screenlib::chunk::sendTextChunks(
        &collectEnvelope,
        &chunks,
        TextChunkKind_TEXT_CHUNK_ATTRIBUTE_CHANGED,
        42,
        1,
        TestPage::kPageId,
        kElementId,
        ElementAttribute_ELEMENT_ATTRIBUTE_TEXT,
        0,
        longText));
    TEST_ASSERT_TRUE(chunks.size() > 1);

    uint8_t seq = 2;
    for (const Envelope& chunk : chunks) {
        TEST_ASSERT_TRUE(pushIncoming(ctx.transport, chunk, seq++));
    }
    ctx.runtime.tick();

    TEST_ASSERT_EQUAL_STRING(longText, ctx.runtime.model().getString(
                                           kElementId,
                                           ElementAttribute_ELEMENT_ATTRIBUTE_TEXT));
}

void test_send_set_attribute_increments_pending() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.transport.clearTx();

    const auto reqId = ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 500));
    TEST_ASSERT_NOT_EQUAL(screenlib::kInvalidRequestId, reqId);
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(ctx.runtime.pendingCommands()));

    // Проверим, что локальная модель обновилась (оптимистичная запись через sendSetAttribute
    // пишется внешним кодом в модель ДО вызова sendSetAttribute — но сам sendSetAttribute
    // в модель не пишет. Мы сейчас проверяем только очередь).

    std::vector<Envelope> out;
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(decodeTxEnvelopes(ctx.transport, out)));
    TEST_ASSERT_EQUAL_UINT32(Envelope_set_element_attribute_tag, out[0].which_payload);
    TEST_ASSERT_EQUAL_UINT32(reqId, out[0].payload.set_element_attribute.request_id);
    TEST_ASSERT_EQUAL_UINT32(1u, out[0].payload.set_element_attribute.session_id);
    TEST_ASSERT_EQUAL_INT32(500, out[0].payload.set_element_attribute.value.int_value);
}

void test_ack_removes_pending() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.transport.clearTx();

    const auto reqId = ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 500));
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(ctx.runtime.pendingCommands()));

    Envelope ack = makeAttributeChangedAck(TestPage::kPageId, /*session=*/1, 1, reqId, 500);
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport, ack, 3));
    ctx.runtime.tick();

    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(ctx.runtime.pendingCommands()));
    // Модель обновилась значением из ACK.
    TEST_ASSERT_EQUAL_INT32(500, ctx.runtime.model().getInt(
        1, ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH));
}

void test_timeout_triggers_link_down() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.transport.clearTx();

    g_now = 1000;
    const auto reqId = ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 123));
    (void)reqId;
    TEST_ASSERT_TRUE(ctx.runtime.linkUp());

    // Чуть меньше таймаута — пока up.
    g_now = 1000 + screenlib::PageRuntime::kLinkTimeoutMs;
    ctx.runtime.tick();
    TEST_ASSERT_TRUE(ctx.runtime.linkUp());

    // Перешагиваем порог.
    g_now = 1000 + screenlib::PageRuntime::kLinkTimeoutMs + 1;
    ctx.runtime.tick();
    TEST_ASSERT_FALSE(ctx.runtime.linkUp());
    TEST_ASSERT_EQUAL_INT(1, g_link.calls);
    TEST_ASSERT_FALSE(g_link.lastUp);
}

void test_overflow_triggers_link_down() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.transport.clearTx();

    // Забиваем очередь до предела.
    for (std::size_t i = 0; i < screenlib::PageRuntime::kMaxPending; ++i) {
        const auto id = ctx.runtime.sendSetAttribute(
            1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH,
                            static_cast<int32_t>(i)));
        TEST_ASSERT_NOT_EQUAL(screenlib::kInvalidRequestId, id);
    }
    TEST_ASSERT_TRUE(ctx.runtime.linkUp());

    // Следующая попытка должна уронить линк.
    const auto overflow = ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 999));
    TEST_ASSERT_EQUAL_UINT32(screenlib::kInvalidRequestId, overflow);
    TEST_ASSERT_FALSE(ctx.runtime.linkUp());
}

void test_button_event_dispatches_to_page() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    // Дадим странице стать shown.
    Envelope snap = makePageSnapshotEnvelope(TestPage::kPageId, 1);
    pushIncoming(ctx.transport, snap, 1);
    ctx.runtime.tick();
    ctx.transport.clearTx();

    Envelope btn = makeButtonEventEnvelope(TestPage::kPageId, 1, 42, ButtonAction_CLICK);
    TEST_ASSERT_TRUE(pushIncoming(ctx.transport, btn, 4));
    ctx.runtime.tick();

    TEST_ASSERT_EQUAL_INT(1, g_stats.buttonCalls);
    TEST_ASSERT_EQUAL_UINT32(42u, g_stats.lastButtonElementId);
    TEST_ASSERT_EQUAL_INT(ButtonAction_CLICK, g_stats.lastButtonAction);
}

void test_stale_button_event_is_ignored() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    // session=1 сейчас
    Envelope btn = makeButtonEventEnvelope(TestPage::kPageId, /*session=*/999, 42);
    pushIncoming(ctx.transport, btn, 1);
    ctx.runtime.tick();
    TEST_ASSERT_EQUAL_INT(0, g_stats.buttonCalls);
}

void test_second_navigate_calls_onClose_and_increments_session() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    const uint32_t s1 = ctx.runtime.currentSessionId();

    ctx.runtime.navigateTo<OtherPage>();
    TEST_ASSERT_EQUAL_INT(1, g_stats.closeCalls);
    TEST_ASSERT_EQUAL_UINT32(OtherPage::kPageId, ctx.runtime.currentPageId());
    TEST_ASSERT_TRUE(ctx.runtime.currentSessionId() > s1);
}

void test_navigate_drains_pending_queue() {
    TestCtx ctx;
    ctx.runtime.navigateTo<TestPage>();
    ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 1));
    ctx.runtime.sendSetAttribute(
        1, makeIntValue(ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH, 2));
    TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(ctx.runtime.pendingCommands()));

    ctx.runtime.navigateTo<OtherPage>();
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(ctx.runtime.pendingCommands()));
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_navigate_sends_showpage_with_new_session);
    RUN_TEST(test_on_show_fires_when_snapshot_arrives);
    RUN_TEST(test_stale_snapshot_is_ignored);
    RUN_TEST(test_page_snapshot_with_long_text_field);
    RUN_TEST(test_send_set_attribute_increments_pending);
    RUN_TEST(test_ack_removes_pending);
    RUN_TEST(test_timeout_triggers_link_down);
    RUN_TEST(test_overflow_triggers_link_down);
    RUN_TEST(test_button_event_dispatches_to_page);
    RUN_TEST(test_stale_button_event_is_ignored);
    RUN_TEST(test_second_navigate_calls_onClose_and_increments_session);
    RUN_TEST(test_navigate_drains_pending_queue);
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
