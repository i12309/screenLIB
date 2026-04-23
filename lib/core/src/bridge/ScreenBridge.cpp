#include "ScreenBridge.h"

#include <string.h>

#include "log/ScreenLibLogger.h"

namespace {

constexpr const char* kBridgeLogTag = "screenlib.bridge";

const char* screenlib_payload_name(pb_size_t tag) {
    switch (tag) {
        case Envelope_show_page_tag: return "show_page";
        case Envelope_set_text_tag: return "set_text";
        case Envelope_set_color_tag: return "set_color";
        case Envelope_set_visible_tag: return "set_visible";
        case Envelope_set_value_tag: return "set_value";
        case Envelope_set_batch_tag: return "set_batch";
        case Envelope_button_event_tag: return "button_event";
        case Envelope_input_event_tag: return "input_event";
        case Envelope_heartbeat_tag: return "heartbeat";
        case Envelope_hello_tag: return "hello";
        case Envelope_request_device_info_tag: return "request_device_info";
        case Envelope_device_info_tag: return "device_info";
        case Envelope_request_current_page_tag: return "request_current_page";
        case Envelope_current_page_tag: return "current_page";
        case Envelope_request_page_state_tag: return "request_page_state";
        case Envelope_page_state_tag: return "page_state";
        case Envelope_request_element_state_tag: return "request_element_state";
        case Envelope_element_state_tag: return "element_state";
        case Envelope_set_element_attribute_tag: return "set_element_attribute";
        case Envelope_request_element_attribute_tag: return "request_element_attribute";
        case Envelope_element_attribute_state_tag: return "element_attribute_state";
        case Envelope_page_snapshot_tag: return "page_snapshot";
        case Envelope_attribute_changed_tag: return "attribute_changed";
        default: return "unknown";
    }
}

}  // namespace

bool ScreenBridge::sendEnvelope(const Envelope& env) {
    const size_t protoLen = ProtoCodec::encode(env, _protoTxBuf, sizeof(_protoTxBuf));
    if (protoLen == 0) {
        SCREENLIB_LOGW(kBridgeLogTag,
                       "tx encode failed payload=%s(%u)",
                       screenlib_payload_name(env.which_payload),
                       static_cast<unsigned>(env.which_payload));
        return false;
    }

    const bool ok = sendFramePayload(_protoTxBuf, protoLen);
    SCREENLIB_LOGT(kBridgeLogTag,
                   "tx payload=%s(%u) proto=%u status=%s",
                   screenlib_payload_name(env.which_payload),
                   static_cast<unsigned>(env.which_payload),
                   static_cast<unsigned>(protoLen),
                   ok ? "ok" : "fail");
    return ok;
}

size_t ScreenBridge::processIncoming() {
    _transport.tick();

    const bool nowConnected = _transport.connected();
    if (nowConnected != _lastConnected) {
        _frameCodec.reset();
        _lastConnected = nowConnected;
        SCREENLIB_LOGI(kBridgeLogTag, "link %s", nowConnected ? "up" : "down");
    }
    if (!nowConnected) {
        return 0;
    }

    size_t processed = 0;
    while (true) {
        const size_t n = _transport.read(_readBuf, sizeof(_readBuf));
        if (n == 0) {
            break;
        }

        _frameCodec.feed(_readBuf, n);

        FrameCodec::Frame frame;
        while (_frameCodec.popFrame(frame)) {
            Envelope env{};
            if (!ProtoCodec::decode(frame.payload, frame.payloadLen, env)) {
                SCREENLIB_LOGW(kBridgeLogTag,
                               "rx decode failed payload_len=%u seq=%u",
                               static_cast<unsigned>(frame.payloadLen),
                               static_cast<unsigned>(frame.seq));
                continue;
            }
            SCREENLIB_LOGT(kBridgeLogTag,
                           "rx payload=%s(%u) payload_len=%u seq=%u",
                           screenlib_payload_name(env.which_payload),
                           static_cast<unsigned>(env.which_payload),
                           static_cast<unsigned>(frame.payloadLen),
                           static_cast<unsigned>(frame.seq));
            dispatchEnvelope(env);
            processed++;
        }
    }

    return processed;
}

bool ScreenBridge::showPage(uint32_t pageId) {
    Envelope env{};
    env.which_payload = Envelope_show_page_tag;
    env.payload.show_page.page_id = pageId;
    return sendEnvelope(env);
}

bool ScreenBridge::setText(uint32_t elementId, const char* text) {
    Envelope env{};
    env.which_payload = Envelope_set_text_tag;
    env.payload.set_text.element_id = elementId;
    copyTextSafe(env.payload.set_text.text, sizeof(env.payload.set_text.text), text);
    return sendEnvelope(env);
}

bool ScreenBridge::setValue(uint32_t elementId, int32_t value) {
    Envelope env{};
    env.which_payload = Envelope_set_value_tag;
    env.payload.set_value.element_id = elementId;
    env.payload.set_value.value = value;
    return sendEnvelope(env);
}

bool ScreenBridge::setVisible(uint32_t elementId, bool visible) {
    Envelope env{};
    env.which_payload = Envelope_set_visible_tag;
    env.payload.set_visible.element_id = elementId;
    env.payload.set_visible.visible = visible;
    return sendEnvelope(env);
}

bool ScreenBridge::setColor(uint32_t elementId, uint32_t bgColor, uint32_t fgColor) {
    Envelope env{};
    env.which_payload = Envelope_set_color_tag;
    env.payload.set_color.element_id = elementId;
    env.payload.set_color.bg_color = bgColor;
    env.payload.set_color.fg_color = fgColor;
    return sendEnvelope(env);
}

bool ScreenBridge::sendHeartbeat(uint32_t uptimeMs) {
    Envelope env{};
    env.which_payload = Envelope_heartbeat_tag;
    env.payload.heartbeat.uptime_ms = uptimeMs;
    return sendEnvelope(env);
}

// Типизированный set одного атрибута с проверкой пары attribute/value.
bool ScreenBridge::setElementAttribute(const SetElementAttribute& attr) {
    SetElementAttribute safeAttr = SetElementAttribute_init_zero;
    if (!sanitizeElementAttribute(attr, safeAttr)) {
        return false;
    }

    Envelope env{};
    env.which_payload = Envelope_set_element_attribute_tag;
    env.payload.set_element_attribute = safeAttr;
    return sendEnvelope(env);
}

// Типизированный get одного атрибута.
bool ScreenBridge::requestElementAttribute(uint32_t elementId,
                                           ElementAttribute attribute,
                                           uint32_t pageId,
                                           uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_request_element_attribute_tag;
    env.payload.request_element_attribute.request_id = requestId;
    env.payload.request_element_attribute.page_id = pageId;
    env.payload.request_element_attribute.element_id = elementId;
    env.payload.request_element_attribute.attribute = attribute;
    return sendEnvelope(env);
}

bool ScreenBridge::sendBatch(const SetBatch& batch) {
    SetBatch safeBatch{};
    sanitizeBatch(batch, safeBatch);

    Envelope env{};
    env.which_payload = Envelope_set_batch_tag;
    env.payload.set_batch = safeBatch;

    if (sendEnvelope(env)) {
        return true;
    }

    return sendBatchSplit(safeBatch);
}

bool ScreenBridge::sendHello(const DeviceInfo& deviceInfo) {
    Envelope env{};
    env.which_payload = Envelope_hello_tag;
    env.payload.hello.has_device_info = true;
    env.payload.hello.device_info = deviceInfo;
    return sendEnvelope(env);
}

bool ScreenBridge::requestDeviceInfo(uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_request_device_info_tag;
    env.payload.request_device_info.request_id = requestId;
    return sendEnvelope(env);
}

bool ScreenBridge::requestCurrentPage(uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_request_current_page_tag;
    env.payload.request_current_page.request_id = requestId;
    return sendEnvelope(env);
}

bool ScreenBridge::requestPageState(uint32_t pageId, uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_request_page_state_tag;
    env.payload.request_page_state.request_id = requestId;
    env.payload.request_page_state.page_id = pageId;
    return sendEnvelope(env);
}

bool ScreenBridge::requestElementState(uint32_t elementId, uint32_t pageId, uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_request_element_state_tag;
    env.payload.request_element_state.request_id = requestId;
    env.payload.request_element_state.page_id = pageId;
    env.payload.request_element_state.element_id = elementId;
    return sendEnvelope(env);
}

bool ScreenBridge::sendDeviceInfo(const DeviceInfo& deviceInfo) {
    Envelope env{};
    env.which_payload = Envelope_device_info_tag;
    env.payload.device_info = deviceInfo;
    return sendEnvelope(env);
}

bool ScreenBridge::sendCurrentPage(uint32_t pageId, uint32_t requestId) {
    Envelope env{};
    env.which_payload = Envelope_current_page_tag;
    env.payload.current_page.request_id = requestId;
    env.payload.current_page.page_id = pageId;
    return sendEnvelope(env);
}

bool ScreenBridge::sendPageState(const PageState& pageState) {
    Envelope env{};
    env.which_payload = Envelope_page_state_tag;
    env.payload.page_state = pageState;
    return sendEnvelope(env);
}

bool ScreenBridge::sendElementState(const ElementState& elementState) {
    Envelope env{};
    env.which_payload = Envelope_element_state_tag;
    env.payload.element_state = elementState;
    return sendEnvelope(env);
}

// Типизированный ответ на request_element_attribute.
bool ScreenBridge::sendElementAttributeState(const ElementAttributeState& state) {
    Envelope env{};
    env.which_payload = Envelope_element_attribute_state_tag;
    env.payload.element_attribute_state = state;
    return sendEnvelope(env);
}

bool ScreenBridge::sendFramePayload(const uint8_t* payload, size_t payloadLen) {
    if (payload == nullptr || payloadLen == 0) {
        return false;
    }
    if (payloadLen > kMaxPayload) {
        return false;
    }

    const size_t frameLen = FrameCodec::pack(
        payload,
        static_cast<uint16_t>(payloadLen),
        _frameTxBuf,
        sizeof(_frameTxBuf),
        _txSeq++,
        kFrameVer
    );

    if (frameLen == 0) {
        return false;
    }

    return _transport.write(_frameTxBuf, frameLen);
}

void ScreenBridge::dispatchEnvelope(const Envelope& env) const {
    if (_handler != nullptr) {
        _handler(env, _handlerUser);
    }
}

void ScreenBridge::copyTextSafe(char* dst, size_t dstSize, const char* src) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

uint8_t ScreenBridge::clampBatchCount(uint8_t value) {
    return value > kMaxBatchCount ? kMaxBatchCount : value;
}

void ScreenBridge::sanitizeBatch(const SetBatch& src, SetBatch& dst) {
    dst = SetBatch{};

    dst.texts_count = clampBatchCount(src.texts_count);
    for (uint8_t i = 0; i < dst.texts_count; i++) {
        dst.texts[i].element_id = src.texts[i].element_id;
        copyTextSafe(dst.texts[i].text, sizeof(dst.texts[i].text), src.texts[i].text);
    }

    dst.colors_count = clampBatchCount(src.colors_count);
    for (uint8_t i = 0; i < dst.colors_count; i++) {
        dst.colors[i] = src.colors[i];
    }

    dst.visibles_count = clampBatchCount(src.visibles_count);
    for (uint8_t i = 0; i < dst.visibles_count; i++) {
        dst.visibles[i] = src.visibles[i];
    }

    dst.values_count = clampBatchCount(src.values_count);
    for (uint8_t i = 0; i < dst.values_count; i++) {
        dst.values[i] = src.values[i];
    }
}

bool ScreenBridge::sendBatchSplit(const SetBatch& batch) {
    for (uint8_t i = 0; i < batch.texts_count; i++) {
        if (!setText(batch.texts[i].element_id, batch.texts[i].text)) {
            return false;
        }
    }

    for (uint8_t i = 0; i < batch.colors_count; i++) {
        if (!setColor(batch.colors[i].element_id, batch.colors[i].bg_color, batch.colors[i].fg_color)) {
            return false;
        }
    }

    for (uint8_t i = 0; i < batch.visibles_count; i++) {
        if (!setVisible(batch.visibles[i].element_id, batch.visibles[i].visible)) {
            return false;
        }
    }

    for (uint8_t i = 0; i < batch.values_count; i++) {
        if (!setValue(batch.values[i].element_id, batch.values[i].value)) {
            return false;
        }
    }

    return true;
}

bool ScreenBridge::sanitizeElementAttribute(const SetElementAttribute& src, SetElementAttribute& dst) {
    dst = SetElementAttribute_init_zero;
    dst.element_id = src.element_id;

    if (src.attribute < _ElementAttribute_MIN || src.attribute > _ElementAttribute_MAX) {
        return false;
    }
    dst.attribute = src.attribute;

    switch (src.attribute) {
        // Числовые атрибуты.
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH:
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_HEIGHT:
        case ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH:
            if (src.which_value != SetElementAttribute_int_value_tag) {
                return false;
            }
            dst.which_value = SetElementAttribute_int_value_tag;
            dst.value.int_value = src.value.int_value;
            return true;

        // Цветовые атрибуты (нормализуем к RGB888).
        case ElementAttribute_ELEMENT_ATTRIBUTE_BACKGROUND_COLOR:
        case ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_COLOR:
        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_COLOR:
            if (src.which_value != SetElementAttribute_color_value_tag) {
                return false;
            }
            dst.which_value = SetElementAttribute_color_value_tag;
            dst.value.color_value = src.value.color_value & 0x00FFFFFFu;
            return true;

        // Шрифтовый атрибут (enum ElementFont).
        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT_FONT:
            if (src.which_value != SetElementAttribute_font_value_tag) {
                return false;
            }
            if (src.value.font_value < _ElementFont_MIN || src.value.font_value > _ElementFont_MAX) {
                return false;
            }
            dst.which_value = SetElementAttribute_font_value_tag;
            dst.value.font_value = src.value.font_value;
            return true;

        case ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN:
        default:
            return false;
    }
}
