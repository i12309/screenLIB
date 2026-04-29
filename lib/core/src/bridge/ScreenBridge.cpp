#include "ScreenBridge.h"

#include <string.h>

#include "log/ScreenLibLogger.h"

namespace {

constexpr const char* kBridgeLogTag = "screenlib.bridge";

const char* screenlib_payload_name(pb_size_t tag) {
    switch (tag) {
        case Envelope_show_page_tag: return "show_page";
        case Envelope_button_event_tag: return "button_event";
        case Envelope_input_event_tag: return "input_event";
        case Envelope_heartbeat_tag: return "heartbeat";
        case Envelope_hello_tag: return "hello";
        case Envelope_request_device_info_tag: return "request_device_info";
        case Envelope_device_info_tag: return "device_info";
        case Envelope_request_current_page_tag: return "request_current_page";
        case Envelope_current_page_tag: return "current_page";
        case Envelope_set_element_attribute_tag: return "set_element_attribute";
        case Envelope_request_element_attribute_tag: return "request_element_attribute";
        case Envelope_element_attribute_state_tag: return "element_attribute_state";
        case Envelope_page_snapshot_tag: return "page_snapshot";
        case Envelope_attribute_changed_tag: return "attribute_changed";
        case Envelope_text_chunk_tag: return "text_chunk";
        case Envelope_text_chunk_abort_tag: return "text_chunk_abort";
        case Envelope_prepare_page_tag: return "prepare_page";
        case Envelope_page_prepared_tag: return "page_prepared";
        case Envelope_apply_page_data_tag: return "apply_page_data";
        case Envelope_page_data_applied_tag: return "page_data_applied";
        case Envelope_commit_page_tag: return "commit_page";
        case Envelope_page_shown_tag: return "page_shown";
        case Envelope_abort_prepared_page_tag: return "abort_prepared_page";
        case Envelope_page_transaction_timeout_tag: return "page_transaction_timeout";
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

Envelope& ScreenBridge::prepareTxEnvelope(pb_size_t payloadTag) {
    memset(&_txEnvelope, 0, sizeof(_txEnvelope));
    _txEnvelope.which_payload = payloadTag;
    return _txEnvelope;
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
            if (!ProtoCodec::decode(frame.payload, frame.payloadLen, _rxEnvelope)) {
                SCREENLIB_LOGW(kBridgeLogTag,
                               "rx decode failed payload_len=%u seq=%u",
                               static_cast<unsigned>(frame.payloadLen),
                               static_cast<unsigned>(frame.seq));
                continue;
            }
            SCREENLIB_LOGT(kBridgeLogTag,
                           "rx payload=%s(%u) payload_len=%u seq=%u",
                           screenlib_payload_name(_rxEnvelope.which_payload),
                           static_cast<unsigned>(_rxEnvelope.which_payload),
                           static_cast<unsigned>(frame.payloadLen),
                           static_cast<unsigned>(frame.seq));
            dispatchEnvelope(_rxEnvelope);
            processed++;
        }
    }

    return processed;
}

bool ScreenBridge::showPage(uint32_t pageId, uint32_t sessionId) {
    Envelope& env = prepareTxEnvelope(Envelope_show_page_tag);
    env.payload.show_page.page_id = pageId;
    env.payload.show_page.session_id = sessionId;
    return sendEnvelope(env);
}

bool ScreenBridge::preparePage(uint32_t pageId,
                               uint32_t sessionId,
                               uint32_t commitTimeoutMs,
                               bool hasInitialData) {
    Envelope& env = prepareTxEnvelope(Envelope_prepare_page_tag);
    env.payload.prepare_page.page_id = pageId;
    env.payload.prepare_page.session_id = sessionId;
    env.payload.prepare_page.commit_timeout_ms = commitTimeoutMs;
    env.payload.prepare_page.has_initial_data = hasInitialData;
    return sendEnvelope(env);
}

bool ScreenBridge::applyPageData(const ApplyPageData& data) {
    Envelope& env = prepareTxEnvelope(Envelope_apply_page_data_tag);
    env.payload.apply_page_data = data;
    return sendEnvelope(env);
}

bool ScreenBridge::commitPage(uint32_t pageId, uint32_t sessionId) {
    Envelope& env = prepareTxEnvelope(Envelope_commit_page_tag);
    env.payload.commit_page.page_id = pageId;
    env.payload.commit_page.session_id = sessionId;
    return sendEnvelope(env);
}

bool ScreenBridge::abortPreparedPage(uint32_t pageId,
                                     uint32_t sessionId,
                                     PageTransitionError reason) {
    Envelope& env = prepareTxEnvelope(Envelope_abort_prepared_page_tag);
    env.payload.abort_prepared_page.page_id = pageId;
    env.payload.abort_prepared_page.session_id = sessionId;
    env.payload.abort_prepared_page.reason = reason;
    return sendEnvelope(env);
}

bool ScreenBridge::sendHeartbeat(uint32_t uptimeMs) {
    Envelope& env = prepareTxEnvelope(Envelope_heartbeat_tag);
    env.payload.heartbeat.uptime_ms = uptimeMs;
    return sendEnvelope(env);
}

// Типизированный set одного атрибута с проверкой пары attribute/value.
bool ScreenBridge::setElementAttribute(const SetElementAttribute& attr) {
    SetElementAttribute safeAttr = SetElementAttribute_init_zero;
    if (!sanitizeElementAttribute(attr, safeAttr)) {
        return false;
    }

    Envelope& env = prepareTxEnvelope(Envelope_set_element_attribute_tag);
    env.payload.set_element_attribute = safeAttr;
    return sendEnvelope(env);
}

// Типизированный get одного атрибута.
bool ScreenBridge::requestElementAttribute(uint32_t elementId,
                                           ElementAttribute attribute,
                                           uint32_t pageId,
                                           uint32_t requestId) {
    Envelope& env = prepareTxEnvelope(Envelope_request_element_attribute_tag);
    env.payload.request_element_attribute.request_id = requestId;
    env.payload.request_element_attribute.page_id = pageId;
    env.payload.request_element_attribute.element_id = elementId;
    env.payload.request_element_attribute.attribute = attribute;
    return sendEnvelope(env);
}

bool ScreenBridge::sendHello(const DeviceInfo& deviceInfo) {
    Envelope& env = prepareTxEnvelope(Envelope_hello_tag);
    env.payload.hello.has_device_info = true;
    env.payload.hello.device_info = deviceInfo;
    return sendEnvelope(env);
}

bool ScreenBridge::requestDeviceInfo(uint32_t requestId) {
    Envelope& env = prepareTxEnvelope(Envelope_request_device_info_tag);
    env.payload.request_device_info.request_id = requestId;
    return sendEnvelope(env);
}

bool ScreenBridge::requestCurrentPage(uint32_t requestId) {
    Envelope& env = prepareTxEnvelope(Envelope_request_current_page_tag);
    env.payload.request_current_page.request_id = requestId;
    return sendEnvelope(env);
}

bool ScreenBridge::sendDeviceInfo(const DeviceInfo& deviceInfo) {
    Envelope& env = prepareTxEnvelope(Envelope_device_info_tag);
    env.payload.device_info = deviceInfo;
    return sendEnvelope(env);
}

bool ScreenBridge::sendCurrentPage(uint32_t pageId, uint32_t requestId) {
    Envelope& env = prepareTxEnvelope(Envelope_current_page_tag);
    env.payload.current_page.request_id = requestId;
    env.payload.current_page.page_id = pageId;
    return sendEnvelope(env);
}

// Типизированный ответ на request_element_attribute.
bool ScreenBridge::sendElementAttributeState(const ElementAttributeState& state) {
    Envelope& env = prepareTxEnvelope(Envelope_element_attribute_state_tag);
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

bool ScreenBridge::sanitizeElementAttribute(const SetElementAttribute& src, SetElementAttribute& dst) {
    dst = SetElementAttribute_init_zero;
    dst.element_id = src.element_id;
    dst.request_id = src.request_id;
    dst.session_id = src.session_id;

    if (src.attribute < _ElementAttribute_MIN || src.attribute > _ElementAttribute_MAX) {
        return false;
    }
    dst.attribute = src.attribute;

    switch (src.attribute) {
        // Числовые атрибуты.
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_WIDTH:
        case ElementAttribute_ELEMENT_ATTRIBUTE_POSITION_HEIGHT:
        case ElementAttribute_ELEMENT_ATTRIBUTE_BORDER_WIDTH:
        case ElementAttribute_ELEMENT_ATTRIBUTE_VALUE:
        case ElementAttribute_ELEMENT_ATTRIBUTE_X:
        case ElementAttribute_ELEMENT_ATTRIBUTE_Y:
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

        case ElementAttribute_ELEMENT_ATTRIBUTE_VISIBLE:
            if (src.which_value != SetElementAttribute_bool_value_tag) {
                return false;
            }
            dst.which_value = SetElementAttribute_bool_value_tag;
            dst.value.bool_value = src.value.bool_value;
            return true;

        case ElementAttribute_ELEMENT_ATTRIBUTE_TEXT:
            return false;

        case ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN:
        default:
            return false;
    }
}
