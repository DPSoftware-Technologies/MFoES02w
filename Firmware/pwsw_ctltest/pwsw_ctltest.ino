#define CTL_PIN 7 // -> ATtiny CTL

#define PULSE_LOW 30 // ms line held low per pulse
#define PULSE_GAP 30 // ms line released between pulses

// pull line low
void ctlLow() {
  pinMode(CTL_PIN, OUTPUT);
  digitalWrite(CTL_PIN, LOW);
}

// release line, ATtiny pullup brings it high
void ctlRelease() {
  pinMode(CTL_PIN, INPUT);
}

// send a command as N pulses
void sendPulses(byte count) {
  for (byte i = 0; i < count; i++) {
    ctlLow();
    delay(PULSE_LOW);
    ctlRelease();
    delay(PULSE_GAP);
  }
  delay(300); // silence so the ATtiny fires the command
}

// named commands
void followStart()  { sendPulses(3); }  // LED mirrors CTL
void followStop()   { sendPulses(4); }  // back to steady LED
void brightUp()     { sendPulses(5); }
void brightDown()   { sendPulses(6); }
void brightReset()  { sendPulses(7); }
void powerOff()     { sendPulses(10); }

void setup() {
  ctlRelease(); // idle
  delay(2000);  // let the ATtiny boot

  brightDown();
  delay(1000);
  brightDown();
  delay(1000);
  brightReset();
  delay(1000);

  // follow mode demo, blink the LED from the sender
  followStart();
  for (byte i = 0; i < 5; i++) {
    ctlLow();  delay(300); // LED on
    ctlRelease(); delay(300); // LED off
  }
  followStop();
  powerOff();
}

void loop() {
}