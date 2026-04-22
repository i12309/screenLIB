#include "runtime/SinglePageRuntime.h"

namespace screenlib {

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
bool Element::requestAttribute(ElementAttribute attribute, uint32_t requestId) {
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
bool Element::requestWidth(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementWidth(_id, _rt->currentPageId(), requestId);
}

bool Element::requestHeight(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementHeight(_id, _rt->currentPageId(), requestId);
}

bool Element::requestBackgroundColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBackgroundColor(_id, _rt->currentPageId(), requestId);
}

bool Element::requestBorderColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBorderColor(_id, _rt->currentPageId(), requestId);
}

bool Element::requestBorderWidth(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementBorderWidth(_id, _rt->currentPageId(), requestId);
}

bool Element::requestTextColor(uint32_t requestId) {
    return _rt != nullptr && _rt->requestElementTextColor(_id, _rt->currentPageId(), requestId);
}

bool Element::requestTextFont(uint32_t requestId) {
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
