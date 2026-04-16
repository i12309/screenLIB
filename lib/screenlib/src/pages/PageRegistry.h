#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IPageController.h"

namespace screenlib {

// Реестр page-контроллеров с маршрутизацией событий в активную страницу.
class PageRegistry {
public:
    // Прикладной лимит количества страниц в реестре.
    static constexpr size_t kMaxPages = 16;

    bool registerPage(IPageController* page);
    bool setCurrentPage(uint32_t pageId);
    uint32_t currentPage() const;

    // Маршрутизировать входящий Envelope в активную страницу.
    // Сейчас в страницы направляются только пользовательские UI-события.
    bool dispatchEnvelope(const Envelope& env, const ScreenEventContext& ctx);

private:
    IPageController* findById(uint32_t pageId) const;
    static bool isUiEvent(pb_size_t tag);

    IPageController* _pages[kMaxPages] = {};
    size_t _count = 0;
    IPageController* _current = nullptr;
    uint32_t _currentPageId = 0;
};

}  // namespace screenlib

