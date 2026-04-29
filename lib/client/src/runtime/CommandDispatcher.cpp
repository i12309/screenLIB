#include "runtime/CommandDispatcher.h"

#include <string.h>

namespace screenlib::client {

bool CommandDispatcher::dispatch(const Envelope& env) {
    switch (env.which_payload) {
        case Envelope_show_page_tag:
            return handleShowPage(env.payload.show_page);

        case Envelope_prepare_page_tag:
            return handlePreparePage(env.payload.prepare_page);

        case Envelope_apply_page_data_tag:
            return handleApplyPageData(env.payload.apply_page_data);

        case Envelope_commit_page_tag:
            return handleCommitPage(env.payload.commit_page);

        case Envelope_abort_prepared_page_tag:
            return handleAbortPreparedPage(env.payload.abort_prepared_page);

        case Envelope_set_element_attribute_tag:
            return handleSetElementAttribute(env.payload.set_element_attribute);

        case Envelope_text_chunk_tag: {
            screenlib::chunk::AssembledText text;
            TextChunkAbort abort = TextChunkAbort_init_zero;
            if (!_textAssembler.push(env.payload.text_chunk, _lastNowMs, text, abort)) {
                return false;
            }
            return handleAssembledText(text);
        }

        default:
            return false;
    }
}

void CommandDispatcher::pollTimeout(uint32_t nowMs) {
    _lastNowMs = nowMs;
    if (!_pending.active || !_pending.pagePrepared) {
        return;
    }
    if (static_cast<int32_t>(nowMs - _pending.commitDeadlineMs) < 0) {
        return;
    }

    const uint32_t pageId = _pending.pageId;
    const uint32_t sessionId = _pending.sessionId;
    const uint32_t waitedMs = nowMs - _pending.startedAtMs;

    _pending = PendingPageTransaction{};
    _timedOutSessionId = sessionId;

    if (_uiAdapter.showPage(0)) {
        _currentPageId = 0;
        _currentSessionId = sessionId;
    }
    sendPageTransactionTimeout(pageId, sessionId, waitedMs);
}

bool CommandDispatcher::sendResponse(const Envelope& env) {
    return _responseSink != nullptr && _responseSink(env, _responseUser);
}

bool CommandDispatcher::sendPagePrepared(uint32_t pageId,
                                         uint32_t sessionId,
                                         bool ok,
                                         PageTransitionError error) {
    Envelope env = Envelope_init_zero;
    env.which_payload = Envelope_page_prepared_tag;
    env.payload.page_prepared.page_id = pageId;
    env.payload.page_prepared.session_id = sessionId;
    env.payload.page_prepared.ok = ok;
    env.payload.page_prepared.error = error;
    return sendResponse(env);
}

bool CommandDispatcher::sendPageDataApplied(uint32_t pageId,
                                            uint32_t sessionId,
                                            uint32_t blockIndex,
                                            bool ok,
                                            uint32_t appliedCount,
                                            uint32_t failedCount,
                                            PageTransitionError error) {
    Envelope env = Envelope_init_zero;
    env.which_payload = Envelope_page_data_applied_tag;
    env.payload.page_data_applied.page_id = pageId;
    env.payload.page_data_applied.session_id = sessionId;
    env.payload.page_data_applied.block_index = blockIndex;
    env.payload.page_data_applied.ok = ok;
    env.payload.page_data_applied.applied_count = appliedCount;
    env.payload.page_data_applied.failed_count = failedCount;
    env.payload.page_data_applied.error = error;
    return sendResponse(env);
}

bool CommandDispatcher::sendPageShown(uint32_t pageId,
                                      uint32_t sessionId,
                                      bool ok,
                                      PageTransitionError error) {
    Envelope env = Envelope_init_zero;
    env.which_payload = Envelope_page_shown_tag;
    env.payload.page_shown.page_id = pageId;
    env.payload.page_shown.session_id = sessionId;
    env.payload.page_shown.ok = ok;
    env.payload.page_shown.error = error;
    return sendResponse(env);
}

bool CommandDispatcher::sendPageTransactionTimeout(uint32_t pageId,
                                                   uint32_t sessionId,
                                                   uint32_t waitedMs) {
    Envelope env = Envelope_init_zero;
    env.which_payload = Envelope_page_transaction_timeout_tag;
    env.payload.page_transaction_timeout.page_id = pageId;
    env.payload.page_transaction_timeout.session_id = sessionId;
    env.payload.page_transaction_timeout.waited_ms = waitedMs;
    return sendResponse(env);
}

bool CommandDispatcher::handleShowPage(const ShowPage& msg) {
    if (pendingMatches(msg.page_id, msg.session_id)) {
        CommitPage commit = CommitPage_init_zero;
        commit.page_id = msg.page_id;
        commit.session_id = msg.session_id;
        return handleCommitPage(commit);
    }

    _pending = PendingPageTransaction{};
    _timedOutSessionId = 0;
    const bool ok = _uiAdapter.showPage(msg.page_id);
    if (ok) {
        _currentPageId = msg.page_id;
        _currentSessionId = msg.session_id;
    }
    return ok;
}

bool CommandDispatcher::handlePreparePage(const PreparePage& msg) {
    _pending = PendingPageTransaction{};
    _textAssembler.reset();

    if (!_uiAdapter.preparePage(msg.page_id)) {
        return sendPagePrepared(msg.page_id,
                                msg.session_id,
                                false,
                                PageTransitionError_PAGE_TRANSITION_UNKNOWN_PAGE);
    }

    _timedOutSessionId = 0;
    _pending.active = true;
    _pending.pageId = msg.page_id;
    _pending.sessionId = msg.session_id;
    _pending.startedAtMs = _lastNowMs;
    _pending.commitDeadlineMs = _lastNowMs + effectiveTimeoutMs(msg.commit_timeout_ms);
    _pending.pagePrepared = true;

    return sendPagePrepared(msg.page_id,
                            msg.session_id,
                            true,
                            PageTransitionError_PAGE_TRANSITION_OK);
}

bool CommandDispatcher::handleApplyPageData(const ApplyPageData& msg) {
    if (!pendingMatches(msg.page_id, msg.session_id)) {
        if (msg.session_id == _timedOutSessionId) {
            return false;
        }
        return sendPageDataApplied(msg.page_id,
                                   msg.session_id,
                                   msg.block_index,
                                   false,
                                   0,
                                   0,
                                   PageTransitionError_PAGE_TRANSITION_BAD_SESSION);
    }

    uint32_t appliedCount = 0;
    uint32_t failedCount = 0;
    for (pb_size_t i = 0; i < msg.elements_count; ++i) {
        applySnapshotElement(msg.elements[i], appliedCount, failedCount);
    }

    const bool ok = appliedCount > 0 || failedCount == 0;
    if (!ok) {
        _pending.dataFailed = true;
    }
    return sendPageDataApplied(msg.page_id,
                               msg.session_id,
                               msg.block_index,
                               ok,
                               appliedCount,
                               failedCount,
                               ok ? PageTransitionError_PAGE_TRANSITION_OK
                                  : PageTransitionError_PAGE_TRANSITION_DATA_FAILED);
}

bool CommandDispatcher::handleCommitPage(const CommitPage& msg) {
    if (!pendingMatches(msg.page_id, msg.session_id)) {
        return false;
    }

    const bool ok = _pending.pagePrepared && !_pending.dataFailed && _uiAdapter.showPage(msg.page_id);
    if (ok) {
        _currentPageId = msg.page_id;
        _currentSessionId = msg.session_id;
        _pending = PendingPageTransaction{};
        return sendPageShown(msg.page_id,
                             msg.session_id,
                             true,
                             PageTransitionError_PAGE_TRANSITION_OK);
    }

    return sendPageShown(msg.page_id,
                         msg.session_id,
                         false,
                         _pending.dataFailed ? PageTransitionError_PAGE_TRANSITION_DATA_FAILED
                                             : PageTransitionError_PAGE_TRANSITION_CREATE_FAILED);
}

bool CommandDispatcher::handleAbortPreparedPage(const AbortPreparedPage& msg) {
    if (!pendingMatches(msg.page_id, msg.session_id)) {
        return false;
    }
    _pending = PendingPageTransaction{};
    _textAssembler.reset();
    return true;
}

bool CommandDispatcher::handleSetElementAttribute(const SetElementAttribute& attr) {
    if (attr.session_id != 0) {
        if (_pending.active && attr.session_id == _pending.sessionId) {
            return _uiAdapter.setElementAttribute(attr);
        }
        if (attr.session_id != _currentSessionId || attr.session_id == _timedOutSessionId) {
            return false;
        }
    }
    return _uiAdapter.setElementAttribute(attr);
}

bool CommandDispatcher::handleAssembledText(const screenlib::chunk::AssembledText& text) {
    if (text.kind != TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE ||
        text.attribute != ElementAttribute_ELEMENT_ATTRIBUTE_TEXT) {
        return false;
    }

    if (text.sessionId != 0) {
        if (_pending.active && text.sessionId == _pending.sessionId) {
            return _uiAdapter.setTextAttribute(text.elementId, text.text.c_str());
        }
        if (text.sessionId != _currentSessionId || text.sessionId == _timedOutSessionId) {
            return false;
        }
    }
    return _uiAdapter.setTextAttribute(text.elementId, text.text.c_str());
}

bool CommandDispatcher::applySnapshotElement(const ElementSnapshot& snapshot,
                                             uint32_t& appliedCount,
                                             uint32_t& failedCount) {
    bool anyApplied = false;
    for (pb_size_t i = 0; i < snapshot.attributes_count; ++i) {
        if (applyAttributeValue(snapshot.element_id, snapshot.attributes[i])) {
            appliedCount++;
            anyApplied = true;
        } else {
            failedCount++;
        }
    }
    return anyApplied;
}

bool CommandDispatcher::applyAttributeValue(uint32_t elementId, const ElementAttributeValue& value) {
    if (value.attribute == ElementAttribute_ELEMENT_ATTRIBUTE_TEXT &&
        value.which_value == ElementAttributeValue_string_value_tag) {
        return _uiAdapter.setTextAttribute(elementId, value.value.string_value);
    }

    SetElementAttribute attr = SetElementAttribute_init_zero;
    attr.element_id = elementId;
    attr.attribute = value.attribute;
    attr.session_id = _pending.sessionId;

    switch (value.which_value) {
        case ElementAttributeValue_int_value_tag:
            attr.which_value = SetElementAttribute_int_value_tag;
            attr.value.int_value = value.value.int_value;
            break;
        case ElementAttributeValue_color_value_tag:
            attr.which_value = SetElementAttribute_color_value_tag;
            attr.value.color_value = value.value.color_value;
            break;
        case ElementAttributeValue_font_value_tag:
            attr.which_value = SetElementAttribute_font_value_tag;
            attr.value.font_value = value.value.font_value;
            break;
        case ElementAttributeValue_bool_value_tag:
            attr.which_value = SetElementAttribute_bool_value_tag;
            attr.value.bool_value = value.value.bool_value;
            break;
        default:
            return false;
    }

    return _uiAdapter.setElementAttribute(attr);
}

uint32_t CommandDispatcher::effectiveTimeoutMs(uint32_t requestedMs) const {
    uint32_t timeout = requestedMs == 0 ? kDefaultCommitTimeoutMs : requestedMs;
    if (timeout < kMinCommitTimeoutMs) {
        timeout = kMinCommitTimeoutMs;
    }
    if (timeout > kMaxCommitTimeoutMs) {
        timeout = kMaxCommitTimeoutMs;
    }
    return timeout;
}

bool CommandDispatcher::pendingMatches(uint32_t pageId, uint32_t sessionId) const {
    return _pending.active && _pending.pageId == pageId && _pending.sessionId == sessionId;
}

}  // namespace screenlib::client
