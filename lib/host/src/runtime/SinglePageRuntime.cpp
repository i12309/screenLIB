#include "runtime/SinglePageRuntime.h"

namespace screenlib {

namespace {

int32_t encode_percent_coord(int32_t percent) {
    constexpr int32_t kCoordTypeShift = 29;
    constexpr int32_t kCoordTypeSpec = (1 << kCoordTypeShift);
    constexpr int32_t kCoordMax = (1 << kCoordTypeShift) - 1;
    constexpr int32_t kPctStoredMax = kCoordMax - 1;
    constexpr int32_t kPctPosMax = kPctStoredMax / 2;

    if (percent < 0) {
        percent = 0;
    } else if (percent > kPctPosMax) {
        percent = kPctPosMax;
    }

    return kCoordTypeSpec | percent;
}

}  // namespace

bool Element::setText(const char* text) {
    return _rt != nullptr && _rt->setText(_id, text);
}

bool Element::setValue(int32_t value) {
    return _rt != nullptr && _rt->setValue(_id, value);
}

bool Element::setVisible(bool visible) {
    return _rt != nullptr && _rt->setVisible(_id, visible);
}

// Универсальный типизированный set с принудительной фиксацией element_id текущего Element.
bool Element::setAttribute(const SetElementAttribute& attr) {
    if (_rt == nullptr) {
        return false;
    }
    SetElementAttribute normalized = attr;
    normalized.element_id = _id;
    return _rt->setElementAttribute(normalized);
}

// Универсальный типизированный get для текущей страницы runtime.
bool Element::getAttribute(ElementAttribute attribute, uint32_t requestId) {
    if (_rt == nullptr) {
        return false;
    }
    return _rt->requestElementAttribute(_id, attribute, _rt->currentPageId(), requestId);
}

// ----- Типизированные вспомогательные методы записи -----
bool Element::setWidth(int32_t value) {
    return _rt != nullptr && _rt->setElementWidth(_id, value);
}

bool Element::setHeight(int32_t value) {
    return _rt != nullptr && _rt->setElementHeight(_id, value);
}

bool Element::setWidthPercent(int32_t percent) {
    return _rt != nullptr && _rt->setElementWidth(_id, encode_percent_coord(percent));
}

bool Element::setHeightPercent(int32_t percent) {
    return _rt != nullptr && _rt->setElementHeight(_id, encode_percent_coord(percent));
}

bool Element::setBackgroundColor(uint32_t rgb888) {
    return _rt != nullptr && _rt->setElementBackgroundColor(_id, rgb888);
}

bool Element::setBorderColor(uint32_t rgb888) {
    return _rt != nullptr && _rt->setElementBorderColor(_id, rgb888);
}

bool Element::setBorderWidth(int32_t value) {
    return _rt != nullptr && _rt->setElementBorderWidth(_id, value);
}

bool Element::setTextColor(uint32_t rgb888) {
    return _rt != nullptr && _rt->setElementTextColor(_id, rgb888);
}

bool Element::setTextFont(ElementFont font) {
    return _rt != nullptr && _rt->setElementTextFont(_id, font);
}

// ----- Типизированные вспомогательные методы запроса -----
bool Element::getWidth(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementWidth(_id, _rt->currentPageId(), requestId);
}

bool Element::getHeight(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementHeight(_id, _rt->currentPageId(), requestId);
}

bool Element::getBackgroundColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBackgroundColor(_id, _rt->currentPageId(), requestId);
}

bool Element::getBorderColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBorderColor(_id, _rt->currentPageId(), requestId);
}

bool Element::getBorderWidth(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBorderWidth(_id, _rt->currentPageId(), requestId);
}

bool Element::getTextColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementTextColor(_id, _rt->currentPageId(), requestId);
}

bool Element::getTextFont(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementTextFont(_id, _rt->currentPageId(), requestId);
}

bool SinglePageRuntime::init(const ScreenConfig& cfg) {
    _screens.setEventHandler(&SinglePageRuntime::onScreenEvent, this);
    _initialized = _screens.init(cfg);
    return _initialized;
}

void SinglePageRuntime::tick() {
    if (!_initialized) {
        return;
    }
    _screens.tick();
    if (_current) {
        _current->onTick();
    }
}

void SinglePageRuntime::onScreenEvent(const Envelope& env,
                                      const ScreenEventContext& ctx,
                                      void* userData) {
    auto* self = static_cast<SinglePageRuntime*>(userData);
    if (self != nullptr) {
        self->dispatch(env, ctx);
    }
}

void SinglePageRuntime::dispatch(const Envelope& env, const ScreenEventContext& ctx) {
    if (_eventObserver != nullptr) {
        _eventObserver(env, ctx, _eventObserverUser);
    }

    (void)ctx;
    if (!_current) {
        return;
    }

    const uint32_t pageId = _current->pageId();

    switch (env.which_payload) {
        case Envelope_button_event_tag: {
            const ButtonEvent& be = env.payload.button_event;
            if (be.page_id != 0 && be.page_id != pageId) {
                return;
            }
            ButtonAction action = be.action;
            if (action < _ButtonAction_MIN || action > _ButtonAction_MAX) {
                action = ButtonAction_CLICK;
            }
            _current->onButton(be.element_id, action);
            break;
        }
        case Envelope_input_event_tag: {
            const InputEvent& ie = env.payload.input_event;
            if (ie.page_id != 0 && ie.page_id != pageId) {
                return;
            }
            if (ie.which_value == InputEvent_int_value_tag) {
                _current->onInputInt(ie.element_id, ie.value.int_value);
            } else if (ie.which_value == InputEvent_string_value_tag) {
                _current->onInputText(ie.element_id, ie.value.string_value);
            }
            break;
        }
        case Envelope_element_attribute_state_tag: {
            const ElementAttributeState& eas = env.payload.element_attribute_state;
            if (eas.page_id != 0 && eas.page_id != pageId) {
                return;
            }
            _current->onElementAttribute(eas);
            break;
        }
        default:
            // Остальные пакеты (page_state, element_state, hello, heartbeat и т.п.)
            // в страницы не маршрутизируются — это инфраструктурный обмен.
            break;
    }
}

// Возвращает runtime на предыдущую страницу, если она была сохранена.
bool SinglePageRuntime::back() {
    if (_previousFactory == nullptr) {
        return false;
    }

    PageFactory previousFactory = _previousFactory;
    _previousFactory = nullptr;
    const bool opened = swapCurrent(previousFactory(), previousFactory);
    if (opened) {
        _previousFactory = nullptr;
    }
    return opened;
}

// Закрывает текущую страницу и открывает новую, сохраняя предыдущую для back().
bool SinglePageRuntime::swapCurrent(std::unique_ptr<IHostPage> next, PageFactory nextFactory) {
    if (!next) {
        return false;
    }

    _previousFactory = _currentFactory;

    if (_current) {
        _current->onClose();
        _current.reset();
    }

    next->_runtime = this;
    _current = std::move(next);
    _currentFactory = nextFactory;

    if (_initialized) {
        _screens.showPage(_current->pageId());
    }
    _current->onShow();
    return true;
}

}  // namespace screenlib
