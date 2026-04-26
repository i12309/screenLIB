#include "chunk/TextChunkAssembler.h"

#include <cstring>

namespace screenlib::chunk {

void TextChunkAssembler::reset() {
    for (ActiveTransfer& transfer : _active) {
        clear(transfer);
    }
}

TextChunkAssembler::ActiveTransfer* TextChunkAssembler::find(uint32_t transferId) {
    for (ActiveTransfer& transfer : _active) {
        if (transfer.used && transfer.transferId == transferId) {
            return &transfer;
        }
    }
    return nullptr;
}

TextChunkAssembler::ActiveTransfer* TextChunkAssembler::allocate() {
    for (ActiveTransfer& transfer : _active) {
        if (!transfer.used) {
            return &transfer;
        }
    }
    return nullptr;
}

void TextChunkAssembler::clear(ActiveTransfer& transfer) {
    transfer.used = false;
    transfer.transferId = 0;
    transfer.sessionId = 0;
    transfer.pageId = 0;
    transfer.elementId = 0;
    transfer.attribute = ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
    transfer.kind = TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE;
    transfer.requestId = 0;
    transfer.chunkCount = 0;
    transfer.lastUpdateMs = 0;
    transfer.text.clear();
    transfer.totalLen = 0;
    transfer.receivedCount = 0;
    std::memset(transfer.received, 0, sizeof(transfer.received));
}

bool TextChunkAssembler::metadataMatches(const ActiveTransfer& transfer, const TextChunk& chunk) {
    return transfer.sessionId == chunk.session_id &&
           transfer.pageId == chunk.page_id &&
           transfer.elementId == chunk.element_id &&
           transfer.attribute == chunk.attribute &&
           transfer.kind == chunk.kind &&
           transfer.requestId == chunk.request_id &&
           transfer.chunkCount == chunk.chunk_count;
}

void TextChunkAssembler::fillAbort(const TextChunk& chunk,
                                   TextChunkAbortReason reason,
                                   TextChunkAbort& abortOut) {
    abortOut = TextChunkAbort_init_zero;
    abortOut.transfer_id = chunk.transfer_id;
    abortOut.request_id = chunk.request_id;
    abortOut.reason = reason;
}

void TextChunkAssembler::fillAbort(const ActiveTransfer& transfer,
                                   TextChunkAbortReason reason,
                                   TextChunkAbort& abortOut) {
    abortOut = TextChunkAbort_init_zero;
    abortOut.transfer_id = transfer.transferId;
    abortOut.request_id = transfer.requestId;
    abortOut.reason = reason;
}

bool TextChunkAssembler::push(const TextChunk& chunk,
                              uint32_t nowMs,
                              AssembledText& out,
                              TextChunkAbort& abortOut) {
    out = AssembledText{};
    abortOut = TextChunkAbort_init_zero;

    if (chunk.transfer_id == 0 ||
        chunk.chunk_count == 0 ||
        chunk.chunk_count > kMaxTextChunkCount ||
        chunk.chunk_index >= chunk.chunk_count) {
        fillAbort(chunk, TextChunkAbortReason_TEXT_CHUNK_ABORT_BAD_INDEX, abortOut);
        return false;
    }

    if (chunk.chunk_data.size > kTextChunkPayloadBytes ||
        static_cast<std::size_t>(chunk.chunk_count) * kTextChunkPayloadBytes > kMaxChunkedTextBytes) {
        fillAbort(chunk, TextChunkAbortReason_TEXT_CHUNK_ABORT_OVERFLOW, abortOut);
        return false;
    }

    if (chunk.kind == TextChunkKind_TEXT_CHUNK_INPUT_EVENT &&
        chunk.attribute != ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN) {
        fillAbort(chunk, TextChunkAbortReason_TEXT_CHUNK_ABORT_METADATA_CHANGED, abortOut);
        return false;
    }

    ActiveTransfer* transfer = find(chunk.transfer_id);
    if (transfer == nullptr) {
        transfer = allocate();
        if (transfer == nullptr) {
            fillAbort(chunk, TextChunkAbortReason_TEXT_CHUNK_ABORT_OVERFLOW, abortOut);
            return false;
        }

        clear(*transfer);
        transfer->used = true;
        transfer->transferId = chunk.transfer_id;
        transfer->sessionId = chunk.session_id;
        transfer->pageId = chunk.page_id;
        transfer->elementId = chunk.element_id;
        transfer->attribute = chunk.attribute;
        transfer->kind = chunk.kind;
        transfer->requestId = chunk.request_id;
        transfer->chunkCount = chunk.chunk_count;
        transfer->text.assign(static_cast<std::size_t>(chunk.chunk_count) * kTextChunkPayloadBytes, '\0');
    } else if (!metadataMatches(*transfer, chunk)) {
        fillAbort(*transfer, TextChunkAbortReason_TEXT_CHUNK_ABORT_METADATA_CHANGED, abortOut);
        clear(*transfer);
        return false;
    }

    const std::size_t offset = static_cast<std::size_t>(chunk.chunk_index) * kTextChunkPayloadBytes;
    const std::size_t end = offset + chunk.chunk_data.size;
    if (end > kMaxChunkedTextBytes || end > transfer->text.size()) {
        fillAbort(*transfer, TextChunkAbortReason_TEXT_CHUNK_ABORT_OVERFLOW, abortOut);
        clear(*transfer);
        return false;
    }

    if (chunk.chunk_data.size > 0) {
        std::memcpy(&transfer->text[offset], chunk.chunk_data.bytes, chunk.chunk_data.size);
    }
    if (!transfer->received[chunk.chunk_index]) {
        transfer->received[chunk.chunk_index] = true;
        transfer->receivedCount++;
    }
    if (end > transfer->totalLen) {
        transfer->totalLen = end;
    }
    transfer->lastUpdateMs = nowMs;

    if (transfer->receivedCount != transfer->chunkCount) {
        return false;
    }

    out.transferId = transfer->transferId;
    out.sessionId = transfer->sessionId;
    out.pageId = transfer->pageId;
    out.elementId = transfer->elementId;
    out.attribute = transfer->attribute;
    out.kind = transfer->kind;
    out.requestId = transfer->requestId;
    out.text.assign(transfer->text.data(), transfer->totalLen);
    clear(*transfer);
    return true;
}

bool TextChunkAssembler::pollTimeout(uint32_t nowMs, TextChunkAbort& abortOut) {
    abortOut = TextChunkAbort_init_zero;
    for (ActiveTransfer& transfer : _active) {
        if (!transfer.used) {
            continue;
        }
        if (nowMs - transfer.lastUpdateMs >= kAssemblyTimeoutMs) {
            fillAbort(transfer, TextChunkAbortReason_TEXT_CHUNK_ABORT_TIMEOUT, abortOut);
            clear(transfer);
            return true;
        }
    }
    return false;
}

}  // namespace screenlib::chunk
