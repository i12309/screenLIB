#include "ScreenEndpoint.h"

namespace screenlib {

void ScreenEndpoint::configure(uint8_t endpointId, EndpointRole role, ScreenBridge* bridge, bool enabled) {
    _id = endpointId;
    _role = role;
    _bridge = bridge;
    _enabled = enabled;

    if (_bridge != nullptr) {
        // Привязываем callback входящих сообщений bridge к endpoint.
        _bridge->setEnvelopeHandler(&ScreenEndpoint::onBridgeEnvelope, this);
    }
}

size_t ScreenEndpoint::tick() {
    if (!canUseBridge()) {
        return 0;
    }
    return _bridge->poll();
}

bool ScreenEndpoint::connected() const {
    return canUseBridge() && _bridge->connected();
}

bool ScreenEndpoint::showPage(uint32_t pageId) {
    return canUseBridge() && _bridge->showPage(pageId);
}

bool ScreenEndpoint::setText(uint32_t elementId, const char* text) {
    return canUseBridge() && _bridge->setText(elementId, text);
}

bool ScreenEndpoint::setValue(uint32_t elementId, int32_t value) {
    return canUseBridge() && _bridge->setValue(elementId, value);
}

bool ScreenEndpoint::setVisible(uint32_t elementId, bool visible) {
    return canUseBridge() && _bridge->setVisible(elementId, visible);
}

bool ScreenEndpoint::sendHeartbeat(uint32_t uptimeMs) {
    return canUseBridge() && _bridge->sendHeartbeat(uptimeMs);
}

bool ScreenEndpoint::sendBatch(const SetBatch& batch) {
    return canUseBridge() && _bridge->sendBatch(batch);
}

bool ScreenEndpoint::requestDeviceInfo(uint32_t requestId) {
    return canUseBridge() && _bridge->requestDeviceInfo(requestId);
}

bool ScreenEndpoint::requestCurrentPage(uint32_t requestId) {
    return canUseBridge() && _bridge->requestCurrentPage(requestId);
}

bool ScreenEndpoint::requestPageState(uint32_t pageId, uint32_t requestId) {
    return canUseBridge() && _bridge->requestPageState(pageId, requestId);
}

bool ScreenEndpoint::canUseBridge() const {
    return _enabled && _bridge != nullptr;
}

ScreenEventContext ScreenEndpoint::makeEventContext() const {
    ScreenEventContext ctx{};
    ctx.endpointId = _id;
    ctx.isPhysical = (_role == EndpointRole::Physical);
    ctx.isWeb = (_role == EndpointRole::Web);
    return ctx;
}

void ScreenEndpoint::onBridgeEnvelope(const Envelope& env, void* userData) {
    ScreenEndpoint* self = static_cast<ScreenEndpoint*>(userData);
    if (self == nullptr || self->_incomingHandler == nullptr) {
        return;
    }

    // Важно: обработчик вызывается синхронно из tick/poll.
    // Тяжёлую бизнес-логику лучше выносить из callback в отдельную очередь задач.
    self->_incomingHandler(env, self->makeEventContext(), self->_incomingUser);
}

}  // namespace screenlib
