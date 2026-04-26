#include "runtime/CommandDispatcher.h"

namespace screenlib::client {

bool CommandDispatcher::dispatch(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_show_page_tag:
            return _uiAdapter.showPage(env.payload.show_page.page_id);

        // Точечный типизированный атрибут элемента (width/height/цвет/шрифт и т.д.).
        case Envelope_set_element_attribute_tag:
            return _uiAdapter.setElementAttribute(env.payload.set_element_attribute);

        case Envelope_text_chunk_tag: {
            screenlib::chunk::AssembledText text;
            TextChunkAbort abort = TextChunkAbort_init_zero;
            if (!_textAssembler.push(env.payload.text_chunk, 0, text, abort)) {
                return false;
            }
            if (text.kind != TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE ||
                text.attribute != ElementAttribute_ELEMENT_ATTRIBUTE_TEXT) {
                return false;
            }
            return _uiAdapter.setTextAttribute(text.elementId, text.text.c_str());
        }

        default:
            // Не пытаемся трактовать как UI-команду (event/heartbeat/unknown).
            return false;
    }
}

}  // namespace screenlib::client
