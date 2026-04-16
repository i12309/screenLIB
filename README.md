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

## Конфиг

Тестовый JSON-конфиг находится в `src/main.cpp` (`kScreenConfigJson`).

