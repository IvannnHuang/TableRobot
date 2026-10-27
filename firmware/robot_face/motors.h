#ifndef _ROBOT_MOTORS_H
#define _ROBOT_MOTORS_H

#include <Arduino.h>

// Two-wheel drive for a 4-input H-bridge board (L298N / L9110S / DRV8833 style).
// Speed is set with PWM on the IN pins. IN1/IN2 drive the left motor, IN3/IN4 the right.
// Speeds are given in percent (-100..100, positive = forward) and ramp smoothly,
// which keeps the robot from jerking and avoids current spikes that can reset the ESP32.
// When a wheel starts from standstill it gets a short full-power kick to break it loose,
// small gear motors often won't start at low power but keep turning once moving.
class Drive
{
public:

  int minPwm;         // lowest PWM (0-255) that still turns the wheels, raise if motors hum but don't move
  int maxPwm;         // highest PWM, lower it to protect motors rated below the supply voltage
  int kickMs;         // full-power pulse when a wheel starts, 0 = off
  int rampPerMs = 1;  // speed units (0..255) changed per millisecond, 1 = ~250ms from stop to full

  Drive(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4, bool invertLeft, bool invertRight,
        int minPwm, int maxPwm, int kickMs, uint32_t pwmFrequency)
    : minPwm(minPwm), maxPwm(maxPwm), kickMs(kickMs),
      in1(in1), in2(in2), in3(in3), in4(in4), invertLeft(invertLeft), invertRight(invertRight),
      pwmFrequency(pwmFrequency) {}

  void begin() {
    pinMode(in1, OUTPUT);
    pinMode(in2, OUTPUT);
    pinMode(in3, OUTPUT);
    pinMode(in4, OUTPUT);
    analogWriteFrequency(in1, pwmFrequency); // sets the frequency used when the pins get PWM
    halt();
    lastUpdate = millis();
  }

  // Set target speed for each wheel in percent, the wheels ramp towards it in update()
  void set(int leftPct, int rightPct) {
    targetL = toUnits(leftPct);
    targetR = toUnits(rightPct);
  }

  // Ramp down to a stop
  void stop() {
    set(0, 0);
  }

  // Stop immediately without ramping
  void halt() {
    targetL = targetR = curL = curR = 0;
    apply();
  }

  // Call every loop
  void update() {
    unsigned long now = millis();
    int maxStep = (int)(now - lastUpdate) * rampPerMs;
    if (maxStep == 0) return;
    lastUpdate = now;
    if (curL == 0 && targetL != 0) kickUntilL = now + kickMs;
    if (curR == 0 && targetR != 0) kickUntilR = now + kickMs;
    curL = approach(curL, targetL, maxStep);
    curR = approach(curR, targetR, maxStep);
    apply();
  }

  bool moving() const {
    return curL != 0 || curR != 0;
  }

private:

  uint8_t in1, in2, in3, in4;
  bool invertLeft, invertRight;
  uint32_t pwmFrequency;
  int targetL = 0, targetR = 0; // -255..255
  int curL = 0, curR = 0;       // -255..255
  unsigned long lastUpdate = 0;
  unsigned long kickUntilL = 0, kickUntilR = 0;

  static int toUnits(int pct) {
    return constrain(pct, -100, 100) * 255 / 100;
  }

  static int approach(int cur, int target, int step) {
    if (cur < target) return min(cur + step, target);
    return max(cur - step, target);
  }

  void apply() {
    unsigned long now = millis();
    bool kickL = (long)(kickUntilL - now) > 0 && curL != 0;
    bool kickR = (long)(kickUntilR - now) > 0 && curR != 0;
    writeMotor(in1, in2, invertLeft ? -curL : curL, kickL);
    writeMotor(in3, in4, invertRight ? -curR : curR, kickR);
  }

  // Drive one motor, positive = forward, 0 = coast
  void writeMotor(uint8_t a, uint8_t b, int speed, bool kick) {
    int pwm = 0;
    if (speed != 0) pwm = kick ? maxPwm : map(abs(speed), 1, 255, minPwm, maxPwm);
    if (speed > 0) {
      analogWrite(a, pwm);
      analogWrite(b, 0);
    } else {
      analogWrite(a, 0);
      analogWrite(b, pwm);
    }
  }
};

#endif
