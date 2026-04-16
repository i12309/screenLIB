# screenLIB

Библиотека и тестовый PlatformIO-проект для работы с экранами через единый протокол:

- `ITransport` (`UART` / `WebSocket`)
- `FrameCodec` (frame + CRC)
- `ProtoCodec` (nanopb `Envelope`)
- `ScreenBridge` (message-level API)
- `ScreenManager` / `ScreenSystem` (маршрутизация `physical/web/both`)

## Структура

- `lib/screenlib/src/` — код библиотеки по слоям:
- `config/`
- `link/`
- `frame/`
- `proto/`
- `screen/`
- `manager/`
- `pages/`
- `system/`
- `src/main.cpp` — тестовый runner для проверки библиотеки.
- `doc/` — рабочие примеры и черновики.

## Сборка

```bash
pio run
```

## Запуск монитора

```bash
pio device monitor
```

## Тесты

В проекте добавлены unit-тесты: `test/test_screenlib_core/test_main.cpp`.

Проверка компиляции тестов без загрузки на плату:

```bash
pio test -e esp32dev --without-uploading --without-testing
```

Запуск тестов на реальной плате:

```bash
pio test -e esp32dev --test-port <PORT>
```

## Конфиг

Тестовый JSON-конфиг находится в `src/main.cpp` (`kScreenConfigJson`).

## Pages Layer

`pages`-слой добавляет маршрутизацию входящих UI-событий в активную страницу:

- `pages/IPageController` — контракт контроллера страницы (`pageId/onEnter/onLeave/onEnvelope`).
- `pages/PageRegistry` — реестр контроллеров и текущая активная страница.

`PageRegistry` опционален: если не подключать его в `ScreenSystem`, библиотека продолжит работать через обычный глобальный `EventHandler`.

Рекомендуемый цикл интеграции:

```cpp
screenlib::ScreenSystem screens;
screenlib::PageRegistry pages;

char err[160] = {};
if (!screens.initFromJson(jsonConfig, err, sizeof(err))) {
    // обработка ошибки
}

screens.setPageRegistry(&pages);
screens.setEventHandler(&onScreenEvent, nullptr); // опционально

for (;;) {
    screens.tick();
}
```

### showPage и PageRegistry

- `ScreenManager::showPage(pageId)` после успешной отправки команды автоматически вызывает `PageRegistry::setCurrentPage(pageId)`, если registry подключен.
- Если `showPage` не отправился ни в один целевой endpoint, current page в registry не меняется.
- Если registry подключен, но `pageId` не зарегистрирован, `showPage` вернет `false`, чтобы не скрывать рассинхрон.

### Политика роутинга событий (строгая, вариант B)

- `PageRegistry` принимает только UI-события (`button_event`, `input_event`).
- Событие передается в активную страницу только если `event.page_id == currentPage()`.
- События с чужим `page_id` игнорируются как запоздалые/неактуальные.
