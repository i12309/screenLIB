#pragma once

#include <cstddef>
#include <cstdint>

#include "proto/machine.pb.h"

namespace screenlib::chunk {

constexpr std::size_t kTextChunkPayloadBytes = 128;
constexpr std::size_t kMaxChunkedTextBytes = 1024;
constexpr std::size_t kMaxTextChunkCount =
    kMaxChunkedTextBytes / kTextChunkPayloadBytes;

using EnvelopeSender = bool (*)(const Envelope& env, void* userData);

bool sendTextChunks(EnvelopeSender sender,
                    void* userData,
                    TextChunkKind kind,
                    uint32_t transferId,
                    uint32_t sessionId,
                    uint32_t pageId,
                    uint32_t elementId,
                    ElementAttribute attribute,
                    uint32_t requestId,
                    const char* text);

}  // namespace screenlib::chunk
