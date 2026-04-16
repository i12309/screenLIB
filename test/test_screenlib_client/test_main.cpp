#include <unity.h>

#include "link/WebSocketClientLink.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

void test_websocket_client_link_rejects_invalid_url() {
    WebSocketClientLink link;
    TEST_ASSERT_FALSE(link.begin("invalid-url"));
}

void test_websocket_client_link_accepts_valid_url_format() {
    WebSocketClientLink link;
    TEST_ASSERT_TRUE(link.begin("ws://127.0.0.1:8181/ws"));
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_websocket_client_link_rejects_invalid_url);
    RUN_TEST(test_websocket_client_link_accepts_valid_url_format);
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

