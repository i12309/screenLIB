#include "PageRegistry.h"

namespace screenlib {

bool PageRegistry::registerPage(IPageController* page) {
    if (page == nullptr || _count >= kMaxPages) {
        return false;
    }

    for (size_t i = 0; i < _count; ++i) {
        // Не допускаем дубликаты ни по указателю, ни по page_id.
        if (_pages[i] == page || _pages[i]->pageId() == page->pageId()) {
            return false;
        }
    }

    _pages[_count++] = page;
    return true;
}

bool PageRegistry::setCurrentPage(uint32_t pageId) {
    IPageController* next = findById(pageId);
    if (next == nullptr) {
        return false;
    }

    if (_current == next) {
        return true;
    }

    if (_current != nullptr) {
        _current->onLeave();
    }

    _current = next;
    _currentPageId = pageId;
    _current->onEnter();
    return true;
}

uint32_t PageRegistry::currentPage() const {
    return (_current != nullptr) ? _currentPageId : 0;
}

bool PageRegistry::dispatchEnvelope(const Envelope& env, const ScreenEventContext& ctx) {
    if (_current == nullptr || !isUiEvent(env.which_payload)) {
        return false;
    }

    uint32_t eventPageId = 0;
    if (!tryGetEventPageId(env, eventPageId)) {
        return false;
    }

    // Строгая политика роутинга:
    // событие обрабатывается только если page_id совпадает с текущей активной страницей.
    if (eventPageId != _currentPageId) {
        return false;
    }

    return _current->onEnvelope(env, ctx);
}

IPageController* PageRegistry::findById(uint32_t pageId) const {
    for (size_t i = 0; i < _count; ++i) {
        if (_pages[i] != nullptr && _pages[i]->pageId() == pageId) {
            return _pages[i];
        }
    }
    return nullptr;
}

bool PageRegistry::isUiEvent(pb_size_t tag) {
    return tag == Envelope_button_event_tag || tag == Envelope_input_event_tag;
}

bool PageRegistry::tryGetEventPageId(const Envelope& env, uint32_t& outPageId) {
    if (env.which_payload == Envelope_button_event_tag) {
        outPageId = env.payload.button_event.page_id;
        return true;
    }

    if (env.which_payload == Envelope_input_event_tag) {
        outPageId = env.payload.input_event.page_id;
        return true;
    }

    return false;
}

}  // namespace screenlib
