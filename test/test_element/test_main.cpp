#include <cstdint>
#include <cstring>

#include <unity.h>

#include "pages/Element.h"

void setUp(void) {}
void tearDown(void) {}

using screenlib::Signal;

namespace {

// ---------- Signal: базовые сценарии ----------

int g_callCount = 0;
int32_t g_lastInt = 0;

void test_signal_default_is_empty() {
    Signal<> s;
    TEST_ASSERT_FALSE(static_cast<bool>(s));
    // emit на пустом сигнале — безопасен (no-op).
    s.emit();
}

void test_signal_void_lambda_trivial_capture() {
    Signal<> s;
    g_callCount = 0;
    s = [] { g_callCount++; };
    TEST_ASSERT_TRUE(static_cast<bool>(s));
    s.emit();
    s.emit();
    TEST_ASSERT_EQUAL_INT(2, g_callCount);
}

void test_signal_value_capture() {
    Signal<> s;
    int captured = 42;
    g_callCount = 0;
    s = [captured] { g_callCount = captured; };
    s.emit();
    TEST_ASSERT_EQUAL_INT(42, g_callCount);
}

void test_signal_pointer_capture_to_external_counter() {
    // Захват указателя — типичный паттерн для доступа к состоянию страницы.
    int counter = 0;
    int* pCounter = &counter;
    Signal<> s;
    s = [pCounter] { (*pCounter)++; };
    s.emit();
    s.emit();
    s.emit();
    TEST_ASSERT_EQUAL_INT(3, counter);
}

void test_signal_with_args() {
    Signal<int32_t, int32_t> s;
    int32_t sum = 0;
    int32_t* pSum = &sum;
    s = [pSum](int32_t a, int32_t b) { *pSum = a + b; };
    s.emit(3, 4);
    TEST_ASSERT_EQUAL_INT32(7, sum);
}

void test_signal_reassignment_replaces_callable() {
    Signal<> s;
    int marker = 0;
    int* pm = &marker;
    s = [pm] { *pm = 1; };
    s.emit();
    TEST_ASSERT_EQUAL_INT(1, marker);

    s = [pm] { *pm = 2; };
    s.emit();
    TEST_ASSERT_EQUAL_INT(2, marker);
}

void test_signal_reset_makes_it_empty() {
    Signal<> s;
    g_callCount = 0;
    s = [] { g_callCount++; };
    s.emit();
    TEST_ASSERT_EQUAL_INT(1, g_callCount);

    s.reset();
    TEST_ASSERT_FALSE(static_cast<bool>(s));
    s.emit();  // no-op
    TEST_ASSERT_EQUAL_INT(1, g_callCount);
}

void test_signal_function_pointer_is_accepted() {
    auto fn = +[] { g_callCount++; };   // + делает из лямбды указатель на функцию
    g_callCount = 0;
    Signal<> s;
    s = fn;
    s.emit();
    s.emit();
    TEST_ASSERT_EQUAL_INT(2, g_callCount);
}

void test_signal_storage_fits_multiple_small_captures() {
    // Проверка, что захват нескольких полей влезает в storage.
    int a = 1, b = 2, c = 3;
    int* pa = &a; int* pb = &b; int* pc = &c;
    int result = 0;
    int* pr = &result;

    Signal<> s;
    s = [pa, pb, pc, pr] { *pr = *pa + *pb + *pc; };
    s.emit();
    TEST_ASSERT_EQUAL_INT(6, result);
}

// ---------- Element типы компилируются ----------
// Инстанцируем с рантаймом nullptr чтобы проверить конструкторы и композицию полей.
// Вызывать operator= нельзя (разыменуется nullptr), но сама компиляция важна.

void test_elements_are_constructible() {
    // Этот тест — в первую очередь compile-time: он дергает конструкторы всех
    // типов элементов, чтобы инстанцировать шаблоны Property.
    auto* page = static_cast<screenlib::IPage*>(nullptr);
    screenlib::Button btn(page, 42);
    screenlib::Panel  pnl(page, 43);
    screenlib::Text   txt(page, 44);

    TEST_ASSERT_EQUAL_UINT32(42, btn.id());
    TEST_ASSERT_EQUAL_UINT32(43, pnl.id());
    TEST_ASSERT_EQUAL_UINT32(44, txt.id());

    // onClick у только что созданной кнопки — пуст.
    TEST_ASSERT_FALSE(static_cast<bool>(btn.onClick));
}

void test_button_onClick_captures_this_like() {
    screenlib::Button btn(static_cast<screenlib::IPage*>(nullptr), 1);

    int clicks = 0;
    int* pClicks = &clicks;
    btn.onClick = [pClicks] { (*pClicks)++; };

    btn.onClick.emit();
    btn.onClick.emit();
    btn.onClick.emit();
    TEST_ASSERT_EQUAL_INT(3, clicks);
}

}  // namespace

void run_all_tests() {
    RUN_TEST(test_signal_default_is_empty);
    RUN_TEST(test_signal_void_lambda_trivial_capture);
    RUN_TEST(test_signal_value_capture);
    RUN_TEST(test_signal_pointer_capture_to_external_counter);
    RUN_TEST(test_signal_with_args);
    RUN_TEST(test_signal_reassignment_replaces_callable);
    RUN_TEST(test_signal_reset_makes_it_empty);
    RUN_TEST(test_signal_function_pointer_is_accepted);
    RUN_TEST(test_signal_storage_fits_multiple_small_captures);
    RUN_TEST(test_elements_are_constructible);
    RUN_TEST(test_button_onClick_captures_this_like);
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
