#pragma once

#include <Arduino.h>

// MksServo485
// Header-only helper for MKS SERVO42D/57D over the official Makerbase RS-485 serial protocol.
// Target: ESP32 + HardwareSerial + RS-485 transceiver.
//
// Covered commands in this file:
//   - 0x82  Set mode
//   - 0x8C  Set response/active behavior
//   - 0x8E  Enable/disable Modbus RTU
//   - 0x30  Read encoder carry + value
//   - 0x31  Read encoder addition (int48_t)
//   - 0x32  Read real-time speed
//   - 0x33  Read pulses
//   - 0x34  Read IO status
//   - 0x35  Read raw encoder addition (int48_t)
//   - 0x90  Set home parameters
//   - 0x91  Go home
//   - 0x92  Set current axis to zero
//   - 0xF4  Relative motion by axis
//   - 0xF5  Absolute motion by axis
//   - 0xF6  Speed mode
//   - 0xFD  Position mode 1 (run by pulses)
//
// Notes:
//   1) This class assumes the Makerbase native serial protocol, not Modbus RTU.
//   2) If your RS-485 module auto-switches DE/RE, pass deRePin = -1 in begin().
//   3) Blocking wait*() methods require UartRSP/respond enabled and active replies enabled.
//   4) The manual has a likely typo in the speed-read response section. readSpeed() accepts
//      both 0x32 and 0x31 as the echoed function code to be tolerant.

class MksServo485 {
public:
  enum class Mode : uint8_t {
    CR_OPEN = 0,
    CR_CLOSE = 1,
    CR_vFOC = 2,
    SR_OPEN = 3,
    SR_CLOSE = 4,
    SR_vFOC = 5,
  };

  enum class Ack : uint8_t {
    Fail = 0,
    Started = 1,
    Completed = 2,
    EndLimitStopped = 3,
    Timeout = 255,
  };

  struct IoStatus {
    uint8_t raw = 0;
    bool in1 = false;
    bool in2 = false;
    bool out1 = false;
    bool out2 = false;
  };

  struct EncoderCarryValue {
    int32_t carry = 0;
    uint16_t value = 0;
  };

  struct HomeParams {
    bool triggerHigh = false;     // HmTrig: false=Low, true=High
    bool directionCcw = false;    // HmDir : false=CW, true=CCW
    uint16_t speedRpm = 100;      // 0..3000
    bool endLimitEnabled = false; // EndLimit: false=disable, true=enable
  };

  struct Settings {
    uint32_t baud = 38400;
    int8_t rxPin = -1;
    int8_t txPin = -1;
    int8_t deRePin = -1;
    bool deReActiveHigh = true;
    uint32_t startupDelayMs = 3000;
    uint32_t responseTimeoutMs = 3000;
    uint32_t interFrameGapUs = 50;
  };

  explicit MksServo485(uint8_t slaveAddress = 1)
      : _addr(slaveAddress) {}

  bool begin(HardwareSerial &serial, const Settings &settings) {
    _serial = &serial;
    _settings = settings;

    if (_settings.deRePin >= 0) {
      pinMode(_settings.deRePin, OUTPUT);
      setReceiveMode();
    }

#if defined(ARDUINO_ARCH_ESP32)
    if (_settings.rxPin >= 0 && _settings.txPin >= 0) {
      _serial->begin(_settings.baud, SERIAL_8N1, _settings.rxPin, _settings.txPin);
    } else {
      _serial->begin(_settings.baud);
    }
#else
    _serial->begin(_settings.baud);
#endif

    const uint32_t start = millis();
    while (!_serial) {
      if (millis() - start > 1000) {
        break;
      }
      delay(1);
    }

    clearRx();
    if (_settings.startupDelayMs > 0) {
      delay(_settings.startupDelayMs);
    }
    return true;
  }

  bool begin(HardwareSerial &serial,
             int8_t rxPin,
             int8_t txPin,
             int8_t deRePin = -1,
             uint32_t baud = 38400,
             bool deReActiveHigh = true,
             uint32_t startupDelayMs = 3000) {
    Settings s;
    s.baud = baud;
    s.rxPin = rxPin;
    s.txPin = txPin;
    s.deRePin = deRePin;
    s.deReActiveHigh = deReActiveHigh;
    s.startupDelayMs = startupDelayMs;
    return begin(serial, s);
  }

  void setSlaveAddress(uint8_t addr) { _addr = addr; }
  uint8_t slaveAddress() const { return _addr; }

  void setResponseTimeout(uint32_t timeoutMs) { _settings.responseTimeoutMs = timeoutMs; }
  uint32_t responseTimeout() const { return _settings.responseTimeoutMs; }

  void clearRx() {
    if (!_serial) {
      return;
    }
    while (_serial->available() > 0) {
      (void)_serial->read();
    }
  }

  static uint8_t checksum8(const uint8_t *buffer, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
      sum += buffer[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
  }

  // ---------------------------------------------------------------------------
  // Setup / configuration
  // ---------------------------------------------------------------------------

  bool setMode(Mode mode, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    const uint8_t payload[1] = {static_cast<uint8_t>(mode)};
    uint8_t status = 0;
    const bool ok = transactStatus(0x82, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool setResponseMode(bool respondEnabled,
                       bool activeEnabled,
                       Ack *ack = nullptr,
                       uint32_t timeoutMs = 0) {
    const uint8_t payload[2] = {
      static_cast<uint8_t>(respondEnabled ? 1 : 0),
      static_cast<uint8_t>(activeEnabled ? 1 : 0)
    };
    uint8_t status = 0;
    const bool ok = transactStatus(0x8C, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool setModbusEnabled(bool enable, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    const uint8_t payload[1] = {static_cast<uint8_t>(enable ? 1 : 0)};
    uint8_t status = 0;
    const bool ok = transactStatus(0x8E, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  // ---------------------------------------------------------------------------
  // Read commands
  // ---------------------------------------------------------------------------

  bool readEncoderCarry(EncoderCarryValue &out, uint32_t timeoutMs = 0) {
    uint8_t payload[6] = {0};
    size_t payloadLen = sizeof(payload);
    if (!transactData(0x30, nullptr, 0, payload, payloadLen, timeoutMs)) {
      return false;
    }

    out.carry = loadBeI32(payload);
    out.value = loadBeU16(payload + 4);
    return true;
  }

  bool readEncoderAddition(int64_t &addition, uint32_t timeoutMs = 0) {
    uint8_t payload[6] = {0};
    size_t payloadLen = sizeof(payload);
    if (!transactData(0x31, nullptr, 0, payload, payloadLen, timeoutMs)) {
      return false;
    }

    addition = signExtend48(loadBeU48(payload));
    return true;
  }

  bool readSpeed(int16_t &speedRpm, uint32_t timeoutMs = 0) {
    static constexpr uint8_t accepted[] = {0x32, 0x31};

    if (!sendRaw(0x32, nullptr, 0)) {
      return false;
    }

    uint8_t payload[2] = {0};
    size_t payloadLen = sizeof(payload);
    if (!readReplyAny(accepted, sizeof(accepted), payload, payloadLen, effectiveTimeout(timeoutMs))) {
      return false;
    }

    speedRpm = loadBeI16(payload);
    return true;
  }

  bool readPulses(int32_t &pulses, uint32_t timeoutMs = 0) {
    uint8_t payload[4] = {0};
    size_t payloadLen = sizeof(payload);
    if (!transactData(0x33, nullptr, 0, payload, payloadLen, timeoutMs)) {
      return false;
    }

    pulses = loadBeI32(payload);
    return true;
  }

  bool readIo(IoStatus &io, uint32_t timeoutMs = 0) {
    uint8_t payload[1] = {0};
    size_t payloadLen = sizeof(payload);
    if (!transactData(0x34, nullptr, 0, payload, payloadLen, timeoutMs)) {
      return false;
    }

    io.raw = payload[0];
    io.in1 = (payload[0] & 0x01) != 0;
    io.in2 = (payload[0] & 0x02) != 0;
    io.out1 = (payload[0] & 0x04) != 0;
    io.out2 = (payload[0] & 0x08) != 0;
    return true;
  }

  bool readRawEncoderAddition(int64_t &addition, uint32_t timeoutMs = 0) {
    uint8_t payload[6] = {0};
    size_t payloadLen = sizeof(payload);
    if (!transactData(0x35, nullptr, 0, payload, payloadLen, timeoutMs)) {
      return false;
    }

    addition = signExtend48(loadBeU48(payload));
    return true;
  }

  bool readIn1(bool &active, uint32_t timeoutMs = 0) {
    IoStatus io;
    if (!readIo(io, timeoutMs)) {
      return false;
    }
    active = io.in1;
    return true;
  }

  bool readIn2(bool &active, uint32_t timeoutMs = 0) {
    IoStatus io;
    if (!readIo(io, timeoutMs)) {
      return false;
    }
    active = io.in2;
    return true;
  }

  // ---------------------------------------------------------------------------
  // Home / zero
  // ---------------------------------------------------------------------------

  bool setHome(const HomeParams &params, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    if (params.speedRpm > 3000) {
      return false;
    }

    const uint8_t payload[4] = {
      static_cast<uint8_t>(params.triggerHigh ? 1 : 0),
      static_cast<uint8_t>(params.directionCcw ? 1 : 0),
      static_cast<uint8_t>(params.speedRpm & 0xFF),
      static_cast<uint8_t>(params.endLimitEnabled ? 1 : 0)
    };

    uint8_t status = 0;
    const bool ok = transactStatus(0x90, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool goHomeStart(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = transactStatus(0x91, nullptr, 0, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool waitGoHomeComplete(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = readStatusReply(0x91, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Completed);
  }

  bool goHomeBlocking(uint32_t startTimeoutMs = 3000,
                      uint32_t completeTimeoutMs = 0,
                      Ack *finalAck = nullptr) {
    Ack startAck = Ack::Timeout;
    if (!goHomeStart(&startAck, startTimeoutMs)) {
      if (finalAck) {
        *finalAck = startAck;
      }
      return false;
    }
    return waitGoHomeComplete(finalAck, completeTimeoutMs);
  }

  bool setCurrentAxisToZero(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = transactStatus(0x92, nullptr, 0, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  // ---------------------------------------------------------------------------
  // Speed mode (0xF6)
  // ---------------------------------------------------------------------------

  bool speedMode(bool dirCw,
                 uint16_t speedRpm,
                 uint8_t acc,
                 Ack *ack = nullptr,
                 uint32_t timeoutMs = 0) {
    if (speedRpm > 3000) {
      return false;
    }

    uint8_t payload[3];
    payload[0] = static_cast<uint8_t>((dirCw ? 1U : 0U) << 7) |
                 static_cast<uint8_t>((speedRpm >> 8) & 0x0F);
    payload[1] = static_cast<uint8_t>(speedRpm & 0xFF);
    payload[2] = acc;

    uint8_t status = 0;
    const bool ok = transactStatus(0xF6, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    if (ok) {
      _lastSpeedDirCw = dirCw;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool stopSpeed(uint8_t acc = 2, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    return speedMode(_lastSpeedDirCw, 0, acc, ack, timeoutMs);
  }

  // ---------------------------------------------------------------------------
  // Position mode 1: run by pulses (0xFD)
  // ---------------------------------------------------------------------------

  bool positionMode1Start(bool dirCw,
                          uint16_t speedRpm,
                          uint8_t acc,
                          uint32_t pulses,
                          Ack *ack = nullptr,
                          uint32_t timeoutMs = 0) {
    if (speedRpm > 3000) {
      return false;
    }

    uint8_t payload[7];
    payload[0] = static_cast<uint8_t>((dirCw ? 1U : 0U) << 7) |
                 static_cast<uint8_t>((speedRpm >> 8) & 0x0F);
    payload[1] = static_cast<uint8_t>(speedRpm & 0xFF);
    payload[2] = acc;
    storeBeU32(payload + 3, pulses);

    uint8_t status = 0;
    const bool ok = transactStatus(0xFD, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool waitPositionMode1Complete(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = readStatusReply(0xFD, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Completed);
  }

  bool positionMode1Blocking(bool dirCw,
                             uint16_t speedRpm,
                             uint8_t acc,
                             uint32_t pulses,
                             uint32_t startTimeoutMs = 3000,
                             uint32_t completeTimeoutMs = 0,
                             Ack *finalAck = nullptr) {
    Ack startAck = Ack::Timeout;
    if (!positionMode1Start(dirCw, speedRpm, acc, pulses, &startAck, startTimeoutMs)) {
      if (finalAck) {
        *finalAck = startAck;
      }
      return false;
    }
    return waitPositionMode1Complete(finalAck, completeTimeoutMs);
  }

  // ---------------------------------------------------------------------------
  // Position mode 3: relative motion by axis (0xF4)
  // Axis is encoder addition, same domain as command 0x31.
  // ---------------------------------------------------------------------------

  bool moveRelativeAxisStart(uint16_t speedRpm,
                             uint8_t acc,
                             int32_t relAxis,
                             Ack *ack = nullptr,
                             uint32_t timeoutMs = 0) {
    if (speedRpm > 3000) {
      return false;
    }

    uint8_t payload[7];
    storeBeU16(payload + 0, speedRpm);
    payload[2] = acc;
    storeBeI32(payload + 3, relAxis);

    uint8_t status = 0;
    const bool ok = transactStatus(0xF4, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool stopRelativeAxis(uint8_t decelAcc = 2, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t payload[7] = {0};
    payload[2] = decelAcc;

    uint8_t status = 0;
    const bool ok = transactStatus(0xF4, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool waitRelativeAxisComplete(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = readStatusReply(0xF4, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Completed);
  }

  bool moveRelativeAxisBlocking(uint16_t speedRpm,
                                uint8_t acc,
                                int32_t relAxis,
                                uint32_t startTimeoutMs = 3000,
                                uint32_t completeTimeoutMs = 0,
                                Ack *finalAck = nullptr) {
    Ack startAck = Ack::Timeout;
    if (!moveRelativeAxisStart(speedRpm, acc, relAxis, &startAck, startTimeoutMs)) {
      if (finalAck) {
        *finalAck = startAck;
      }
      return false;
    }
    return waitRelativeAxisComplete(finalAck, completeTimeoutMs);
  }

  // ---------------------------------------------------------------------------
  // Position mode 4: absolute motion by axis (0xF5)
  // Axis is encoder addition, same domain as command 0x31.
  // ---------------------------------------------------------------------------

  bool moveAbsoluteAxisStart(uint16_t speedRpm,
                             uint8_t acc,
                             int32_t absAxis,
                             Ack *ack = nullptr,
                             uint32_t timeoutMs = 0) {
    if (speedRpm > 3000) {
      return false;
    }

    uint8_t payload[7];
    storeBeU16(payload + 0, speedRpm);
    payload[2] = acc;
    storeBeI32(payload + 3, absAxis);

    uint8_t status = 0;
    const bool ok = transactStatus(0xF5, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool stopAbsoluteAxis(uint8_t decelAcc = 2, Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t payload[7] = {0};
    payload[2] = decelAcc;

    uint8_t status = 0;
    const bool ok = transactStatus(0xF5, payload, sizeof(payload), status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Started);
  }

  bool waitAbsoluteAxisComplete(Ack *ack = nullptr, uint32_t timeoutMs = 0) {
    uint8_t status = 0;
    const bool ok = readStatusReply(0xF5, status, timeoutMs);
    if (ack) {
      *ack = ok ? static_cast<Ack>(status) : Ack::Timeout;
    }
    return ok && status == static_cast<uint8_t>(Ack::Completed);
  }

  bool moveAbsoluteAxisBlocking(uint16_t speedRpm,
                                uint8_t acc,
                                int32_t absAxis,
                                uint32_t startTimeoutMs = 3000,
                                uint32_t completeTimeoutMs = 0,
                                Ack *finalAck = nullptr) {
    Ack startAck = Ack::Timeout;
    if (!moveAbsoluteAxisStart(speedRpm, acc, absAxis, &startAck, startTimeoutMs)) {
      if (finalAck) {
        *finalAck = startAck;
      }
      return false;
    }
    return waitAbsoluteAxisComplete(finalAck, completeTimeoutMs);
  }

  // ---------------------------------------------------------------------------
  // Low-level helpers
  // ---------------------------------------------------------------------------

  bool sendRaw(uint8_t command, const uint8_t *payload, size_t payloadLen) {
    if (!_serial || payloadLen > kMaxPayloadLen) {
      return false;
    }

    uint8_t frame[3 + kMaxPayloadLen + 1];
    const size_t frameLen = 3 + payloadLen + 1;

    frame[0] = 0xFA;
    frame[1] = _addr;
    frame[2] = command;
    for (size_t i = 0; i < payloadLen; ++i) {
      frame[3 + i] = payload[i];
    }
    frame[3 + payloadLen] = checksum8(frame, 3 + payloadLen);

    clearRx();
    setTransmitMode();
    if (_settings.interFrameGapUs > 0) {
      delayMicroseconds(_settings.interFrameGapUs);
    }
    const size_t written = _serial->write(frame, frameLen);
    _serial->flush();
    if (_settings.interFrameGapUs > 0) {
      delayMicroseconds(_settings.interFrameGapUs);
    }
    setReceiveMode();

    return written == frameLen;
  }

  bool readReply(uint8_t expectedCommand,
                 uint8_t *payload,
                 size_t &payloadLen,
                 uint32_t timeoutMs = 0) {
    return readReplyAny(&expectedCommand, 1, payload, payloadLen, effectiveTimeout(timeoutMs));
  }

private:
  static constexpr size_t kMaxPayloadLen = 16;

  HardwareSerial *_serial = nullptr;
  Settings _settings{};
  uint8_t _addr = 1;
  bool _lastSpeedDirCw = true;

  void setTransmitMode() {
    if (_settings.deRePin < 0) {
      return;
    }
    digitalWrite(_settings.deRePin, _settings.deReActiveHigh ? HIGH : LOW);
  }

  void setReceiveMode() {
    if (_settings.deRePin < 0) {
      return;
    }
    digitalWrite(_settings.deRePin, _settings.deReActiveHigh ? LOW : HIGH);
  }

  uint32_t effectiveTimeout(uint32_t timeoutMs) const {
    return timeoutMs == 0 ? _settings.responseTimeoutMs : timeoutMs;
  }

  static bool commandAccepted(uint8_t command, const uint8_t *accepted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      if (accepted[i] == command) {
        return true;
      }
    }
    return false;
  }

  bool waitForByte(uint8_t &out, uint32_t timeoutMs, bool seekHeader) {
    const uint32_t start = millis();
    while (true) {
      while (_serial->available() > 0) {
        const int v = _serial->read();
        if (v < 0) {
          continue;
        }
        out = static_cast<uint8_t>(v);
        if (!seekHeader || out == 0xFB) {
          return true;
        }
      }

      if (timeoutMs != 0 && (millis() - start) >= timeoutMs) {
        return false;
      }
      delay(1);
    }
  }

  bool readReplyAny(const uint8_t *acceptedCommands,
                    size_t acceptedCommandCount,
                    uint8_t *payload,
                    size_t &payloadLen,
                    uint32_t timeoutMs) {
    if (!_serial) {
      return false;
    }

    uint8_t header = 0;
    if (!waitForByte(header, timeoutMs, true)) {
      return false;
    }

    uint8_t addr = 0;
    uint8_t command = 0;
    if (!waitForByte(addr, timeoutMs, false) || !waitForByte(command, timeoutMs, false)) {
      return false;
    }

    if (addr != _addr) {
      return false;
    }
    if (!commandAccepted(command, acceptedCommands, acceptedCommandCount)) {
      return false;
    }

    uint8_t local[kMaxPayloadLen] = {0};
    if (payloadLen > sizeof(local)) {
      return false;
    }

    for (size_t i = 0; i < payloadLen; ++i) {
      if (!waitForByte(local[i], timeoutMs, false)) {
        return false;
      }
    }

    uint8_t cs = 0;
    if (!waitForByte(cs, timeoutMs, false)) {
      return false;
    }

    uint8_t verify[3 + kMaxPayloadLen] = {0};
    verify[0] = 0xFB;
    verify[1] = addr;
    verify[2] = command;
    for (size_t i = 0; i < payloadLen; ++i) {
      verify[3 + i] = local[i];
    }

    const uint8_t expectedCs = checksum8(verify, 3 + payloadLen);
    if (cs != expectedCs) {
      return false;
    }

    for (size_t i = 0; i < payloadLen; ++i) {
      payload[i] = local[i];
    }
    return true;
  }

  bool transactStatus(uint8_t command,
                      const uint8_t *requestPayload,
                      size_t requestPayloadLen,
                      uint8_t &status,
                      uint32_t timeoutMs) {
    if (!sendRaw(command, requestPayload, requestPayloadLen)) {
      return false;
    }
    return readStatusReply(command, status, timeoutMs);
  }

  bool transactData(uint8_t command,
                    const uint8_t *requestPayload,
                    size_t requestPayloadLen,
                    uint8_t *responsePayload,
                    size_t responsePayloadLen,
                    uint32_t timeoutMs) {
    if (!sendRaw(command, requestPayload, requestPayloadLen)) {
      return false;
    }
    size_t len = responsePayloadLen;
    return readReply(command, responsePayload, len, timeoutMs);
  }

  bool readStatusReply(uint8_t command, uint8_t &status, uint32_t timeoutMs) {
    uint8_t payload[1] = {0};
    size_t len = sizeof(payload);
    if (!readReply(command, payload, len, timeoutMs)) {
      return false;
    }
    status = payload[0];
    return true;
  }

  static uint16_t loadBeU16(const uint8_t *p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
  }

  static int16_t loadBeI16(const uint8_t *p) {
    return static_cast<int16_t>(loadBeU16(p));
  }

  static int32_t loadBeI32(const uint8_t *p) {
    const uint32_t v = (static_cast<uint32_t>(p[0]) << 24) |
                       (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) |
                       static_cast<uint32_t>(p[3]);
    return static_cast<int32_t>(v);
  }

  static uint64_t loadBeU48(const uint8_t *p) {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8) |
           static_cast<uint64_t>(p[5]);
  }

  static int64_t signExtend48(uint64_t v) {
    if ((v & (1ULL << 47)) != 0) {
      v |= 0xFFFF000000000000ULL;
    }
    return static_cast<int64_t>(v);
  }

  static void storeBeU16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
  }

  static void storeBeU32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
  }

  static void storeBeI32(uint8_t *p, int32_t v) {
    storeBeU32(p, static_cast<uint32_t>(v));
  }
};
