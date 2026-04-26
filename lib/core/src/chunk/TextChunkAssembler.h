#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "chunk/TextChunkSender.h"
#include "proto/machine.pb.h"

namespace screenlib::chunk {

struct AssembledText {
    uint32_t transferId = 0;
    uint32_t sessionId = 0;
    uint32_t pageId = 0;
    uint32_t elementId = 0;
    ElementAttribute attribute = ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
    TextChunkKind kind = TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE;
    uint32_t requestId = 0;
    std::string text;
};

class TextChunkAssembler {
public:
    static constexpr std::size_t kMaxActiveTransfers = 4;
    static constexpr uint32_t kAssemblyTimeoutMs = 3000;

    bool push(const TextChunk& chunk,
              uint32_t nowMs,
              AssembledText& out,
              TextChunkAbort& abortOut);

    bool pollTimeout(uint32_t nowMs, TextChunkAbort& abortOut);
    void reset();

private:
    struct ActiveTransfer {
        bool used = false;
        uint32_t transferId = 0;
        uint32_t sessionId = 0;
        uint32_t pageId = 0;
        uint32_t elementId = 0;
        ElementAttribute attribute = ElementAttribute_ELEMENT_ATTRIBUTE_UNKNOWN;
        TextChunkKind kind = TextChunkKind_TEXT_CHUNK_SET_ATTRIBUTE;
        uint32_t requestId = 0;
        uint32_t chunkCount = 0;
        uint32_t lastUpdateMs = 0;
        std::string text;
        std::size_t totalLen = 0;
        uint8_t receivedCount = 0;
        bool received[kMaxTextChunkCount] = {};
    };

    ActiveTransfer _active[kMaxActiveTransfers];

    ActiveTransfer* find(uint32_t transferId);
    ActiveTransfer* allocate();
    void clear(ActiveTransfer& transfer);
    static bool metadataMatches(const ActiveTransfer& transfer, const TextChunk& chunk);
    static void fillAbort(const TextChunk& chunk,
                          TextChunkAbortReason reason,
                          TextChunkAbort& abortOut);
    static void fillAbort(const ActiveTransfer& transfer,
                          TextChunkAbortReason reason,
                          TextChunkAbort& abortOut);
};

}  // namespace screenlib::chunk
