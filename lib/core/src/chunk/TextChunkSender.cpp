#include "chunk/TextChunkSender.h"

#include <cstring>
#include <memory>

namespace screenlib::chunk {

bool sendTextChunks(EnvelopeSender sender,
                    void* userData,
                    TextChunkKind kind,
                    uint32_t transferId,
                    uint32_t sessionId,
                    uint32_t pageId,
                    uint32_t elementId,
                    ElementAttribute attribute,
                    uint32_t requestId,
                    const char* text) {
    if (sender == nullptr || transferId == 0) {
        return false;
    }

    const char* safeText = text != nullptr ? text : "";
    const std::size_t len = std::strlen(safeText);
    if (len > kMaxChunkedTextBytes) {
        return false;
    }

    const std::size_t chunkCount =
        len == 0 ? 1 : ((len + kTextChunkPayloadBytes - 1) / kTextChunkPayloadBytes);
    if (chunkCount == 0 || chunkCount > kMaxTextChunkCount) {
        return false;
    }

    std::unique_ptr<Envelope> env(new Envelope);

    for (std::size_t index = 0; index < chunkCount; ++index) {
        const std::size_t offset = index * kTextChunkPayloadBytes;
        const std::size_t remaining = len > offset ? (len - offset) : 0;
        const std::size_t partLen =
            remaining < kTextChunkPayloadBytes ? remaining : kTextChunkPayloadBytes;

        std::memset(env.get(), 0, sizeof(*env));
        env->which_payload = Envelope_text_chunk_tag;
        TextChunk& chunk = env->payload.text_chunk;
        chunk.transfer_id = transferId;
        chunk.session_id = sessionId;
        chunk.page_id = pageId;
        chunk.element_id = elementId;
        chunk.attribute = attribute;
        chunk.chunk_index = static_cast<uint32_t>(index);
        chunk.chunk_count = static_cast<uint32_t>(chunkCount);
        chunk.chunk_data.size = static_cast<pb_size_t>(partLen);
        if (partLen > 0) {
            std::memcpy(chunk.chunk_data.bytes, safeText + offset, partLen);
        }
        chunk.request_id = requestId;
        chunk.kind = kind;

        if (!sender(*env, userData)) {
            return false;
        }
    }

    return true;
}

}  // namespace screenlib::chunk
