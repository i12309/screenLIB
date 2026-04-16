#pragma once

#include <stdint.h>

#include "types/ScreenTypes.h"
#include "proto/machine.pb.h"

namespace screenlib {

// Интерфейс контроллера страницы.
// Контроллер работает только на уровне Envelope и контекста события.
class IPageController {
public:
    virtual ~IPageController() = default;

    virtual uint32_t pageId() const = 0;
    virtual void onEnter() {}
    virtual void onLeave() {}
    virtual bool onEnvelope(const Envelope& env, const ScreenEventContext& ctx) = 0;
};

}  // namespace screenlib
