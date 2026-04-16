# screenLIB

Библиотека и тестовый PlatformIO-проект для обмена с экранами через единый протокол:

- `ITransport` (`UartLink`, `WebSocketLink`, `WebSocketClientLink`)
- `FrameCodec` (кадр + CRC)
- `ProtoCodec` (nanopb `Envelope`)
- `ScreenBridge` (message-level API)
- `ScreenManager` / `ScreenSystem` (маршрутизация `physical/web/both`)
- `PageRegistry` (роутинг UI-событий в активную страницу)

## Структура

- `lib/screenlib/src/config`
- `lib/screenlib/src/link`
- `lib/screenlib/src/frame`
- `lib/screenlib/src/proto`
- `lib/screenlib/src/screen`
- `lib/screenlib/src/manager`
- `lib/screenlib/src/pages`
- `lib/screenlib/src/system`
- `src/main.cpp` — пример запуска.
- `test/test_screenlib_core/test_main.cpp` — unit-тесты.

## Сборка и тесты

```bash
pio run
pio test -e esp32dev --without-uploading --without-testing
```

## Конфиг

`ScreenSystem` принимает типизированный `ScreenConfig` или JSON (`initFromJson`).

Для `ws_client` используется поле `url`:

```json
{
  "outputs": {
    "web": {
      "enabled": true,
      "type": "ws_client",
      "url": "ws://127.0.0.1:8181/ws"
    }
  },
  "routing": {
    "defaultTarget": "web"
  }
}
```

## Pages Layer

- `ScreenManager::showPage(pageId)` после успешной отправки синхронизирует `PageRegistry::setCurrentPage(pageId)`, если registry подключен.
- Если `showPage` не отправился ни в один endpoint, текущая страница registry не меняется.
- Если registry подключен, но `pageId` не зарегистрирован, `showPage` вернет `false`.

Политика роутинга событий (строгая):

- `PageRegistry` обрабатывает только `button_event` и `input_event`.
- Событие передается в активную страницу только если `event.page_id == currentPage()`.
- События с чужим `page_id` игнорируются как устаревшие.

## WsClient

- Добавлен отдельный `link/WebSocketClientLink` (клиентский `ITransport`).
- `WebSocketLink` (server-side) оставлен отдельным и не смешивается с клиентским transport.
- `ScreenSystem` умеет bootstrap `OutputType::WsClient` для `physical` и `web`.

## EMsdk/Demo Интеграция

Рекомендуемый путь для `EMsdk/DEMO`:

- Платформенный слой demo дает байтовый transport (read/write/tick).
- Поверх transport работает тот же стек: `FrameCodec + ProtoCodec`.
- UI-слой принимает `Envelope` и обновляет экран по `page_id/element_id/value`.
- Прямые ad-hoc команды в UI-слое не используются.

