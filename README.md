# screenLIB

`screenLIB` — репозиторий протокола и runtime-слоев.

Он содержит только транспорт, протокол, runtime и общие абстракции:
- `core`: `ITransport`, frame/proto codec, protobuf-схемы (`machine.proto`, `machine.pb.*`), `ScreenBridge`;
- `host`: host-side runtime объектной модели (`PageRuntime`, `PageModel`, `Element`, `IPage`, host transports);
- `client`: screen-side protocol runtime (`ScreenClient`, `CommandDispatcher`, client transports);
- `adapter`: только API (`IUiAdapter`).

В этом репозитории не должно быть concrete frontend UI-интеграций:
- LVGL/EEZ adapter;
- generated frontend UI;
- object map;
- UI generators.

Все эти части принадлежат репозиторию `ScreenUI`.

## Границы ответственности

- `screenLIB` должен оставаться независимым от конкретного UI framework.
- `client` может зависеть только от интерфейса `IUiAdapter`.
- Concrete adapter implementation подключается снаружи.

## Сборка и тесты

```bash
pio run
pio test -e native
```
