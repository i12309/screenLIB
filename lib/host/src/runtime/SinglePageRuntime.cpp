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
            _current->onButton(be.element_id);
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
