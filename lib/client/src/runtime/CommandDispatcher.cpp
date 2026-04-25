#include "runtime/CommandDispatcher.h"

namespace screenlib::client {

bool CommandDispatcher::dispatch(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_show_page_tag:
            return _uiAdapter.showPage(env.payload.show_page.page_id);

        // Точечный типизированный атрибут элемента (width/height/цвет/шрифт и т.д.).
        case Envelope_set_element_attribute_tag:
            return _uiAdapter.setElementAttribute(env.payload.set_element_attribute);

        default:
            // Не пытаемся трактовать как UI-команду (event/heartbeat/unknown).
            return false;
    }
}

}  // namespace screenlib::client
