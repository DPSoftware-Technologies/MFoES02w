#include <avr/pgmspace.h>

#define LED 0 // -> BTN LED (PWM)
#define OUT 1 // -> MOSFET
#define CTL 2 // <- MCU
#define TRG 3 // -> MCU
#define BTN 4 // <- BTN

#define SAFELONGPRESS 1000 // safe to shutdown
#define CUTLONGPRESS 5000 // cut power instantly

bool isOn = false;
bool followMode = false;
bool isTRG = false;
bool held = false;
unsigned long lastCtlTime = 0;
byte ctlPulses = 0;
bool lastCtlState = HIGH;
byte dimLevel = 4; // brightness step 0-4, start at full

// set LED to current brightness step
void setLedSteady() {
  static const byte lv[5] PROGMEM = {30, 80, 140, 200, 255}; // PWM values, dim to full
  analogWrite(LED, pgm_read_byte(&lv[dimLevel]));
}

void setup() {
  pinMode(OUT, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(TRG, OUTPUT);
  pinMode(BTN, INPUT_PULLUP);
  pinMode(CTL, INPUT_PULLUP);

  digitalWrite(OUT, LOW);
  digitalWrite(LED, LOW);
  digitalWrite(TRG, LOW);
}

void loop() {
  bool currentCtlState = digitalRead(CTL);
  
  if (lastCtlState == HIGH && currentCtlState == LOW) { // Falling edge
    if (millis() - lastCtlTime > 400) {
      ctlPulses = 1;
    } else {
      ctlPulses++;
    }
    lastCtlTime = millis();
  }
  lastCtlState = currentCtlState;

  // pulse command
  if (ctlPulses > 0 && (millis() - lastCtlTime > 200)) {
    if (ctlPulses == 3) { // start follow ctl input if 3 pulse
      followMode = true;  
    } else if (ctlPulses == 4) { // stop follow ctl input if 4 pulse
      followMode = false; 
      if (isOn) setLedSteady();
      else digitalWrite(LED, LOW);
    } else if (ctlPulses == 5) { // brightness up if 5 pulse
      if (dimLevel < 4) dimLevel++;
      if (isOn && !followMode) setLedSteady();
    } else if (ctlPulses == 6) { // brightness down if 6 pulse
      if (dimLevel > 0) dimLevel--;
      if (isOn && !followMode) setLedSteady();
    } else if (ctlPulses == 7) { // reset to full brightness if 7 pulse
      dimLevel = 4;
      if (isOn && !followMode) setLedSteady();
    } else if (ctlPulses == 10) { // power off if 10 pulse
      isOn = false;
      followMode = false;
      digitalWrite(OUT, LOW);
      digitalWrite(LED, LOW);
    }

    ctlPulses = 0;
  }

  // follow ctl input for LED override
  if (followMode) digitalWrite(LED, currentCtlState == LOW ? HIGH : LOW);

  if (digitalRead(BTN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BTN) == LOW) {
      unsigned long startPress = millis();

      if (!isOn) {
        // on when button pressed and not power on
        isOn = true;
        digitalWrite(OUT, HIGH);
        if (!followMode) setLedSteady();

        while (digitalRead(BTN) == LOW);
        delay(50);
      } else {
        // on
        isTRG = false;
        held = false;
        startPress = millis(); // restart timer at the moment hold begins
        while (digitalRead(BTN) == LOW) {
          unsigned long pressTime = millis() - startPress;

          // Hold 5s then cut power
          if (pressTime > CUTLONGPRESS) {
            isOn = false;
            followMode = false;
            digitalWrite(OUT, LOW);
            digitalWrite(LED, LOW);
            digitalWrite(TRG, LOW);
            held = true;

            while (digitalRead(BTN) == LOW);
            delay(50);
            break;
          }

          // Hold 1s then trigger long TRG once
          if (pressTime > SAFELONGPRESS && !isTRG) {
            isTRG = true; // set before the delay so it can never fire twice
            held = true; // long press, so skip the short press trigger
            digitalWrite(TRG, HIGH);
            delay(500);
            digitalWrite(TRG, LOW);
          }
        }

        // trigger when short press
        if (!held && isOn) { 
          digitalWrite(TRG, HIGH);
          delay(50);
          digitalWrite(TRG, LOW);
        }
      }
    }
  }
}