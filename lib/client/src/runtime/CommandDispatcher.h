#pragma once

#include "IUiAdapter.h"
#include "chunk/TextChunkAssembler.h"

namespace screenlib::client {

// Диспетчер команд экрана: Envelope -> вызовы IUiAdapter.
// Не знает ничего про транспорт, framing и protobuf-кодек.
class CommandDispatcher {
public:
    using ResponseSink = bool (*)(const Envelope& env, void* userData);

    explicit CommandDispatcher(screenlib::adapter::IUiAdapter& uiAdapter)
        : _uiAdapter(uiAdapter) {}
    CommandDispatcher(screenlib::adapter::IUiAdapter& uiAdapter, ResponseSink sink, void* userData)
        : _uiAdapter(uiAdapter), _responseSink(sink), _responseUser(userData) {}

    // Применить входящую команду к UI.
    // Возвращает true только если это поддержанная экранная команда
    // и она успешно применена адаптером.
    bool dispatch(const Envelope& env);
    void pollTimeout(uint32_t nowMs);

private:
    struct PendingPageTransaction {
        bool active = false;
        uint32_t pageId = 0;
        uint32_t sessionId = 0;
        uint32_t startedAtMs = 0;
        uint32_t commitDeadlineMs = 0;
        bool pagePrepared = false;
        bool dataFailed = false;
    };

    static constexpr uint32_t kMinCommitTimeoutMs = 300;
    static constexpr uint32_t kDefaultCommitTimeoutMs = 1500;
    static constexpr uint32_t kMaxCommitTimeoutMs = 8000;

    screenlib::adapter::IUiAdapter& _uiAdapter;
    screenlib::chunk::TextChunkAssembler _textAssembler;
    ResponseSink _responseSink = nullptr;
    void* _responseUser = nullptr;
    PendingPageTransaction _pending;
    uint32_t _currentPageId = 0;
    uint32_t _currentSessionId = 0;
    uint32_t _lastNowMs = 0;
    uint32_t _timedOutSessionId = 0;

    bool sendResponse(const Envelope& env);
    bool sendPagePrepared(uint32_t pageId,
                          uint32_t sessionId,
                          bool ok,
                          PageTransitionError error);
    bool sendPageDataApplied(uint32_t pageId,
                             uint32_t sessionId,
                             uint32_t blockIndex,
                             bool ok,
                             uint32_t appliedCount,
                             uint32_t failedCount,
                             PageTransitionError error);
    bool sendPageShown(uint32_t pageId,
                       uint32_t sessionId,
                       bool ok,
                       PageTransitionError error);
    bool sendPageTransactionTimeout(uint32_t pageId, uint32_t sessionId, uint32_t waitedMs);

    bool handleShowPage(const ShowPage& msg);
    bool handlePreparePage(const PreparePage& msg);
    bool handleApplyPageData(const ApplyPageData& msg);
    bool handleCommitPage(const CommitPage& msg);
    bool handleAbortPreparedPage(const AbortPreparedPage& msg);
    bool handleSetElementAttribute(const SetElementAttribute& attr);
    bool handleAssembledText(const screenlib::chunk::AssembledText& text);
    bool applySnapshotElement(const ElementSnapshot& snapshot,
                              uint32_t& appliedCount,
                              uint32_t& failedCount);
    bool applyAttributeValue(uint32_t elementId, const ElementAttributeValue& value);
    uint32_t effectiveTimeoutMs(uint32_t requestedMs) const;
    bool pendingMatches(uint32_t pageId, uint32_t sessionId) const;
};

}  // namespace screenlib::client
