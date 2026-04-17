# screenLIB

`screenLIB` is the protocol/runtime repository.

It contains only transport/protocol/runtime layers and shared abstractions:
- `core`: `ITransport`, frame/proto codecs, protobuf schema (`machine.proto`, `machine.pb.*`), `ScreenBridge`.
- `host`: host-side runtime (`ScreenSystem`, `ScreenManager`, `ScreenEndpoint`, page registry/controllers, host transports).
- `client`: screen-side protocol runtime (`ScreenClient`, `CommandDispatcher`, client transports).
- `adapter`: API only (`IUiAdapter`).

It does not contain concrete frontend UI integrations (LVGL/EEZ adapter, generated frontend UI, UI object maps, UI generators).
Those belong to the `ScreenUI` repository.

## Boundaries

- Keep `screenLIB` UI-framework agnostic.
- `client` depends on `IUiAdapter` interface only.
- Concrete adapter implementations are external dependencies.

## Build and Tests

```bash
pio run
pio test -e native
```
