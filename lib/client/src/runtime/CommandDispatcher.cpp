#include "runtime/CommandDispatcher.h"

namespace screenlib::client {

bool CommandDispatcher::dispatch(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_show_page_tag:
            return _uiAdapter.showPage(env.payload.show_page.page_id);

        case Envelope_set_text_tag:
            return _uiAdapter.setText(
                env.payload.set_text.element_id,
                env.payload.set_text.text
            );

        case Envelope_set_value_tag:
            return _uiAdapter.setValue(
                env.payload.set_value.element_id,
                env.payload.set_value.value
            );

        case Envelope_set_visible_tag:
            return _uiAdapter.setVisible(
                env.payload.set_visible.element_id,
                env.payload.set_visible.visible
            );

        case Envelope_set_color_tag:
            return _uiAdapter.setColor(
                env.payload.set_color.element_id,
                env.payload.set_color.bg_color,
                env.payload.set_color.fg_color
            );

        case Envelope_set_batch_tag:
            return _uiAdapter.applyBatch(env.payload.set_batch);
        // Точечный типизированный атрибут элемента (width/height/цвет/шрифт и т.д.).
        case Envelope_set_element_attribute_tag:
            return _uiAdapter.setElementAttribute(env.payload.set_element_attribute);

        default:
            // Не пытаемся трактовать как UI-команду (event/heartbeat/unknown).
            return false;
    }
}

}  // namespace screenlib::client
