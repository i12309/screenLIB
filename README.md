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

- `IUiAdapter.h` (общий контракт для client runtime)
- `lvgl_eez/EezLvglAdapter.h/.cpp` (реализация под EEZ/LVGL)
- `lvgl_eez/UiObjectMap.h/.cpp` (mapping `page_id/element_id -> UI target/object`)
- `lvgl_eez/` содержит конкретную реализацию адаптера под LVGL/EEZ
- здесь же будут другие UI/platform adapters

## Роли модулей

- `host` не использует client-side transport как runtime-выход.
- `client` держит transport/runtime для экранной стороны.
- `adapter` отвечает только за UI-адаптеры и mapping к конкретному UI runtime.
- `ScreenSystem` остается host-side фасадом.

## Client Runtime

`ScreenClient` — runtime экранной стороны (screen-side), отдельный от `ScreenSystem`.

- Принимает входящие `Envelope` команды через `ScreenBridge` и применяет их в `IUiAdapter`.
- Отправляет обратно только пользовательские события: `button_event` и `input_event`.
- `init()` идемпотентный: повторный вызов безопасен.
- UI-адаптер можно подключать/менять через `setUiAdapter(...)`.

Минимальная цепочка:

```text
ITransport -> ScreenClient -> IUiAdapter
```

## Protocol Model

В протоколе используются два класса сообщений:

- `push UI commands` (основной путь синхронизации UI): `show_page`, `set_text`, `set_value`, `set_visible`, `set_color`, `set_batch`.
- `service request/response` (metadata, snapshot, диагностика): `hello`, `request_device_info`/`device_info`, `request_current_page`/`current_page`, `request_page_state`/`page_state`, `request_element_state`/`element_state`.

Правило источника истины:

- backend — источник истины для динамического UI состояния (обычная работа остается push-driven).
- screen client — источник metadata и локального состояния страницы по запросу (service layer).

Пример service usage (backend -> screen -> backend):

```cpp
// Host side:
screens.requestCurrentPage(/*requestId=*/1001);

// В event handler придет Envelope_current_page_tag с page_id и request_id.
```

Пример handshake hello/device_info (screen side):

```cpp
DeviceInfo info = DeviceInfo_init_zero;
info.protocol_version = 1;
strncpy(info.fw_version, "1.0.0", sizeof(info.fw_version) - 1);
strncpy(info.ui_version, "ui-2026-04", sizeof(info.ui_version) - 1);
strncpy(info.client_type, "esp32-screen", sizeof(info.client_type) - 1);

screenClient.sendHello(info);
```

## EezLvglAdapter

`EezLvglAdapter` — первый concrete `IUiAdapter` для EEZ/LVGL.

- Применяет входящие команды `show_page/set_text/set_value/set_visible/set_color/set_batch` к UI через `UiObjectMap`.
- `UiObjectMap` хранит только mapping `page_id/element_id` к UI-target/object и не содержит transport/proto логики.
- Пользовательские UI-события отправляются наружу только как `button_event`/`input_event` через `EventSink`.
- Сам адаптер остается чистым UI-слоем и не зависит от host/runtime-классов.

Рабочая цепочка для экранной стороны:

```text
ITransport -> ScreenClient -> EezLvglAdapter -> EEZ/LVGL UI
```

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
