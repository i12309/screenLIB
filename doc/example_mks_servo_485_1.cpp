#include <Arduino.h>
#include "MksServo485.h"

HardwareSerial MotorUart(2);
MksServo485 servo(0x01);

static constexpr int PIN_RX = 16;
static constexpr int PIN_TX = 17;
static constexpr int PIN_DE_RE = 4;

void printIo(const MksServo485::IoStatus &io) {
  Serial.print("IO raw=0x");
  Serial.print(io.raw, HEX);
  Serial.print(" IN1=");
  Serial.print(io.in1);
  Serial.print(" IN2=");
  Serial.print(io.in2);
  Serial.print(" OUT1=");
  Serial.print(io.out1);
  Serial.print(" OUT2=");
  Serial.println(io.out2);
}

bool initServo() {
  if (!servo.begin(MotorUart, PIN_RX, PIN_TX, PIN_DE_RE, 38400)) {
    Serial.println("servo.begin failed");
    return false;
  }

  // Фирменный serial protocol, не Modbus
  if (!servo.setModbusEnabled(false)) {
    Serial.println("setModbusEnabled(false) failed");
    return false;
  }

  // Включить ответы и active replies, чтобы blocking-методы могли ждать завершения
  if (!servo.setResponseMode(true, true)) {
    Serial.println("setResponseMode failed");
    return false;
  }

  // Serial + FOC + encoder
  if (!servo.setMode(MksServo485::Mode::SR_vFOC)) {
    Serial.println("setMode(SR_vFOC) failed");
    return false;
  }

  Serial.println("servo init OK");
  return true;
}

bool demoReadStatus() {
  MksServo485::IoStatus io;
  if (!servo.readIo(io)) {
    Serial.println("readIo failed");
    return false;
  }
  printIo(io);

  int16_t speed = 0;
  if (servo.readSpeed(speed)) {
    Serial.print("speed rpm = ");
    Serial.println(speed);
  } else {
    Serial.println("readSpeed failed");
  }

  int32_t pulses = 0;
  if (servo.readPulses(pulses)) {
    Serial.print("pulses = ");
    Serial.println(pulses);
  } else {
    Serial.println("readPulses failed");
  }

  int64_t axis = 0;
  if (servo.readEncoderAddition(axis)) {
    Serial.print("axis = ");
    Serial.println((long long)axis);
  } else {
    Serial.println("readEncoderAddition failed");
  }

  return true;
}

bool demoSpeedMode() {
  Serial.println("speed mode CW 120 rpm");
  if (!servo.speedMode(true, 120, 2)) {
    Serial.println("speedMode start failed");
    return false;
  }

  delay(2000);

  Serial.println("speed stop");
  if (!servo.stopSpeed(2)) {
    Serial.println("stopSpeed failed");
    return false;
  }

  delay(500);
  return true;
}

bool demoPositionMode1() {
  Serial.println("position mode 1: 3200 pulses");
  MksServo485::Ack ack = MksServo485::Ack::Timeout;
  if (!servo.positionMode1Blocking(true, 120, 20, 3200, 3000, 10000, &ack)) {
    Serial.print("positionMode1Blocking failed, ack=");
    Serial.println((int)ack);
    return false;
  }

  Serial.println("position mode 1 done");
  return true;
}

bool demoAbsoluteAxis() {
  int64_t axis = 0;
  if (!servo.readEncoderAddition(axis)) {
    Serial.println("cannot read current axis");
    return false;
  }

  const int32_t target = static_cast<int32_t>(axis + 5000);
  Serial.print("moveAbsoluteAxis to ");
  Serial.println(target);

  MksServo485::Ack ack = MksServo485::Ack::Timeout;
  if (!servo.moveAbsoluteAxisBlocking(120, 20, target, 3000, 10000, &ack)) {
    Serial.print("moveAbsoluteAxisBlocking failed, ack=");
    Serial.println((int)ack);
    return false;
  }

  Serial.println("absolute axis done");
  return true;
}

bool demoHome() {
  MksServo485::HomeParams hp;
  hp.triggerHigh = false;     // hmTrig = Low active
  hp.directionCcw = false;    // искать home в сторону CW
  hp.speedRpm = 60;
  hp.endLimitEnabled = true;

  if (!servo.setHome(hp)) {
    Serial.println("setHome failed");
    return false;
  }

  MksServo485::Ack ack = MksServo485::Ack::Timeout;
  if (!servo.goHomeBlocking(3000, 15000, &ack)) {
    Serial.print("goHomeBlocking failed, ack=");
    Serial.println((int)ack);
    return false;
  }

  Serial.println("goHome done");
  return true;
}

bool demoMarkOffset() {
  Serial.println("move until IN1 then go +1500 axis counts");

  if (!servo.speedMode(true, 80, 2)) {
    Serial.println("speedMode failed");
    return false;
  }

  const uint32_t started = millis();
  while (millis() - started < 10000) {
    bool in1 = false;
    if (!servo.readIn1(in1)) {
      Serial.println("readIn1 failed");
      servo.stopSpeed(2);
      return false;
    }

    if (in1) {
      Serial.println("IN1 triggered");
      servo.stopSpeed(2);
      delay(200);

      int64_t axis = 0;
      if (!servo.readEncoderAddition(axis)) {
        Serial.println("readEncoderAddition failed after trigger");
        return false;
      }

      const int32_t target = static_cast<int32_t>(axis + 1500);
      if (!servo.moveAbsoluteAxisBlocking(60, 10, target, 3000, 10000)) {
        Serial.println("offset move failed");
        return false;
      }

      Serial.println("offset move complete");
      return true;
    }

    delay(10);
  }

  servo.stopSpeed(2);
  Serial.println("mark not found");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  if (!initServo()) {
    return;
  }

  demoReadStatus();
  demoSpeedMode();
  demoPositionMode1();
  demoAbsoluteAxis();

  // Включай только если реально подключен home/концевик
  // demoHome();

  // Пример твоей логики с меткой/датчиком на IN1
  // demoMarkOffset();
}

void loop() {
}
