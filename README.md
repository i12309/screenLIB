# screenLIB

Библиотека для обмена с экранами по единому framed protobuf-протоколу (nanopb), разделенная на 4 модуля:

- `core` — общий протокольный слой.
- `host` — controller/runtime на стороне станка.
- `client` — runtime на стороне экрана (web/WASM/ESP32 screen-side).
- `adapter` — platform/UI адаптеры.

## Структура

```text
lib/
  core/
  host/
  client/
  adapter/
```

### `core`

- `link/ITransport.h`
- `frame/FrameCodec.h/.cpp`
- `proto/machine.proto`, `machine.options`, `machine.pb.h/.c`
- `proto/ProtoCodec.h/.cpp`
- `bridge/ScreenBridge.h/.cpp`
- `types/ScreenTypes.h`

### `host`

- `config/ScreenConfig*`
- `manager/ScreenEndpoint*`
- `manager/ScreenManager*`
- `pages/IPageController*`
- `pages/PageRegistry*`
- `system/ScreenSystem*`
- `link/UartLink*`
- `link/WebSocketServerLink*`

### `client`

- `link/WebSocketClientLink.h/.cpp`
- `runtime/ScreenClient.h/.cpp`
- `runtime/CommandDispatcher.h/.cpp`

### `adapter`

- `lvgl_eez/IUiAdapter.h`
- `lvgl_eez/EezLvglAdapter.h` (заготовка)
- `lvgl_eez/UiObjectMap.h` (mapping `element_id -> UI object`)
- здесь же будут LVGL/EEZ и другие UI/platform adapters

## Роли модулей

- `host` не использует client-side transport как runtime-выход.
- `client` держит transport/runtime для экранной стороны.
- `adapter` отвечает только за UI-адаптеры и mapping к конкретному UI runtime.
- `ScreenSystem` остается host-side фасадом.

Если в host-конфиге приходит `OutputType::WsClient`, `ScreenSystem::init(...)` возвращает ошибку:

```text
ws_client is client-side transport
```

## Быстрый сценарий интеграции (host)

```cpp
screenlib::ScreenSystem screens;

char errBuf[160] = {};
if (!screens.initFromJson(jsonConfig, errBuf, sizeof(errBuf))) {
    // обработка ошибки
}

screens.setEventHandler(&onScreenEvent, nullptr);

void loop() {
    screens.tick();
}
```

`ScreenSystem` сам поднимает runtime для активных host-выходов:

- `uart` (через `UartLink`, модуль `host`)
- `ws_server` (через `WebSocketServerLink`, модуль `host`)

## Pages layer

- Можно подключить `PageRegistry` через `ScreenSystem::setPageRegistry(...)`.
- `showPage(pageId)` синхронизирует активную страницу registry после успешной отправки хотя бы в один endpoint.
- Политика строгая: `PageRegistry` роутит `button_event/input_event` только если `event.page_id == currentPage()`.

## Сборка и тесты

```bash
pio run
pio test -e esp32dev --without-uploading --without-testing
```

Текущий `src/main.cpp` оставлен как demo/bootstrap пример.
