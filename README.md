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
