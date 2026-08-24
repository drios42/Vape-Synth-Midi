/* 
Vape Synth by David Rios, Shuang Cai, Kari Love
Documentation and 3d Print files can be found on the project Github Repo:
https://github.com/drios42/Vape-Synth-Midi

Hardware used: 
  - Reclaimed NexBar casing and LiPo battery
  - Seeed Studio Xiao ESP32-S3
  - hx710B Air pressure sensor module

Code Adapted from the Following resources and examples:
  ESP32-BLE-MIDI by Maxime André
  https://github.com/max22-/ESP32-BLE-MIDI

  hx710B by Homemade Circuits:
  https://www.homemade-circuits.com/hx710b-air-pressure-sensor-datasheet-how-to-connect/

Dependency Issues:
  Only works with NimBLE-Arduino v1.4.3
  esp32 3.3.0 board defs (last checked) 6/25/2026
*/


#include <Arduino.h>
#include <BLEMidi.h>
#include "mappings.h" // notation to midi map
#include "driver/rtc_io.h"

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
#define USE_EXT0_WAKEUP 0  // enable external wakeup 0 is for external

// All pin numbers refer to GPIO numbers, not mapped digital "D" numbers
#define WAKEUP_GPIO GPIO_NUM_4 // GPIO4 -> D3
RTC_DATA_ATTR int bootCount = 0; // delete later not necessary

/*breathe sensor comm pins from breadboard*/
#define sckPin 7    // GPIO7 -> D8
#define dataPin 44  // GPIO44 -> D7

// #define a sensor THRESHOLD. Example uses 1000000
long THRESHOLD = 900000;  // adjust as needed
long bTHRESHOLD = 10000;  // baseline threshold
bool calibrated = false;  // calibration state

long ptime = 0; // Calibration timer variable
int interval = 10000; // Interval for calibration duration

// for drift and threshold calculations
int driftcounter = 0;
int sum = 0;

// variables for state changes and neutral breathing states
long pbreathe = 0;
long baseline;
long baselineavg = 0;
bool playing = false; // Boolean for chord playing states
int dir = 0;
int pdir = 0;
int pnote = -1;
int note = 0;

// variables for reading all the button states as bitmask (4 buttons, 4 bits)
byte creadings = B1111;
byte preadings = B1111;

//MIDI SETUP
int rootNote = C2;  // middle C, maps to a MIDI 36
int channel = 1;

// Major and Minor intervals
int intervalMajor[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
int intervalMinor[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };

// bitmask of 4 button states
byte result = B0000;  // button results as bitmask

/*buttons*/
int pstates[4] = {};
int states[4] = {};
int numButtons = 4;
int buttonPins[4] = {1,2,3,5}; // GPIO PINS numbers
// int buttonPins[4] = { 5, 6, 43, 44 }; // breadboard

// Sleep and Calibration Variables
int resetButtonPin = 4;
int pReset = 1;
int cReset = 1;
// Variables for calibration and recalibration
long sensorHIGH = 0;
long sensorLOW = 100000000;
long threshHIGH = 0;
long threshLOW = 100000000;

// sleep and led blink indicator
int sleepInterval = 3000; // Duration of press before sleeping
int pSleepTime = 0;
bool ledOn = false;
int ledInterval = 500;
long pLed = 0;
int ledPin = 21; // On board LED GPIO21

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("Initializing bluetooth");
  BLEMidiServer.begin("Vape Synth");
  Serial.println("Waiting for connections...");
  //BLEMidiServer.enableDebugging();  // Uncomment if you want to see some debugging output from the library (not much for the server class...)
  pinMode(resetButtonPin, INPUT_PULLUP);
  
  /* note / chord buttons*/
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    states[i] = digitalRead(buttonPins[i]);
    pstates[i] = digitalRead(buttonPins[i]);
  }
  /*breathe sensor*/
  pinMode(dataPin, INPUT);  // Connect HX710 OUT to Arduino pin 10
  pinMode(sckPin, OUTPUT);  // Connect HX710 SCK to Arduino pin 8

  /*LED*/
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  //readings//
  creadings = readButtons();
  preadings = -1;  // button readings
  ptime = millis();
  pReset = digitalRead(resetButtonPin);
  cReset = digitalRead(resetButtonPin);

  print_wakeup_reason();
#if USE_EXT0_WAKEUP
  esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);

#else
  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(WAKEUP_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);
  rtc_gpio_pullup_en(WAKEUP_GPIO);

#endif
  Serial.println("NOT Going to sleep now");
  //esp_deep_sleep_start();
  Serial.println("This will  be printed");
}

void calibrateSensor() {
  while (digitalRead(dataPin)) {}
  long breathe = 0;
  for (int i = 0; i < 24; i++) {
    digitalWrite(sckPin, HIGH);
    digitalWrite(sckPin, LOW);
    breathe = breathe << 1;
    if (digitalRead(dataPin)) {
      breathe++;
    }
  }
  // get the 2s compliment
  breathe = breathe ^ 0x800000;
  // pulse the clock line 3 times to start the next pressure reading
  for (char i = 0; i < 3; i++) {
    digitalWrite(sckPin, HIGH);
    digitalWrite(sckPin, LOW);
  }
  long breathSensor = long(breathe);  // raw breath value

  // get high and low ranges for mapping
  if (breathSensor < sensorLOW) {
    sensorLOW = breathSensor;
  } else if (breathSensor > sensorHIGH) {
    sensorHIGH = breathSensor;
  }
  // check for a neutral reading as baseline
  if (abs(pbreathe - breathSensor) < bTHRESHOLD) {
    sum += breathSensor;
    driftcounter++;
    if (driftcounter >= 30) {
      baselineavg = sum / 30;
      driftcounter = 0;
      sum = 0;
    }
    baseline = baselineavg;
  }
  pbreathe = breathSensor;
  Serial.print("min: ");
  Serial.print(sensorLOW);
  Serial.print(" max: ");
  Serial.print(sensorHIGH);
  Serial.print(" baseline: ");
  Serial.println(baseline);

  if (millis() - ptime > interval) {
    threshHIGH = baseline + ((sensorHIGH - baseline) * 0.1);
    threshLOW = baseline - ((baseline - sensorLOW) * 0.1);
    THRESHOLD = ((sensorHIGH - baseline) * 0.1) + ((baseline - sensorLOW) * 0.1);
    calibrated = true;
    Serial.print("Threshold low ");
    Serial.print(threshLOW);
    Serial.print(" Threshold High ");
    Serial.println(threshHIGH);
    Serial.print(" Threshold ");
    Serial.println(THRESHOLD);
    Serial.print(" baseline ");
    Serial.println(baseline);
    Serial.println("done calibrating");
  }
}
int pResetButton = 1;
bool startSleepTimer = false;

void loop() {
  //  initial calibration
  if (!calibrated) {
    calibrateSensor();
  }
  cReset = digitalRead(resetButtonPin);

  if (cReset == 0 && startSleepTimer) {
    if (millis() - pSleepTime > sleepInterval) {
      // pSleepTime = millis();
      startSleepTimer = false;
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
      Serial.println("Going to sleep now");
      delay(5000);
      esp_deep_sleep_start();
      Serial.println("sleeping");
    }
  }

  // calibrate before connecting
  if (pReset != cReset) {
    if (cReset == 1) {
      calibrated = false;
      startSleepTimer = false;
      sensorHIGH = 0;
      sensorLOW = 100000000;
      THRESHOLD = 800000;
      ptime = millis();
      Serial.println("calibrating");
    }
    if (cReset == 0) {
      startSleepTimer = true;
      pSleepTime = millis();
    }
    pReset = cReset;
  }
  if (!calibrated) {
    calibrateSensor();
    Serial.print(sensorLOW);
    Serial.print(" ");
    Serial.print(baseline);
    Serial.print(" ");
    Serial.println(sensorHIGH);
  }

  if (BLEMidiServer.isConnected()) {  // If we've got a connection, we send an A4 during one second, at full velocity (127)
    if (pReset != cReset) {
      if (cReset == 1) {
        calibrated = false;
        sensorHIGH = 0;
        sensorLOW = 100000000;
        THRESHOLD = 800000;
        ptime = millis();
        Serial.println("calibrating");
      }
      pReset = cReset;
    }
    if (!calibrated) {
      calibrateSensor();
      Serial.print(sensorLOW);
      Serial.print(" ");
      Serial.print(baseline);
      Serial.print(" ");
      Serial.println(sensorHIGH);
    } else {
      digitalWrite(ledPin, HIGH);
      /*breath sensor*/
      while (digitalRead(dataPin)) {}
      long result = 0;
      for (int i = 0; i < 24; i++) {
        digitalWrite(sckPin, HIGH);
        digitalWrite(sckPin, LOW);
        result = result << 1;
        if (digitalRead(dataPin)) {
          result++;
        }
      }
      //Serial.println(result);
      // get the 2s compliment
      result = result ^ 0x800000;
      // pulse the clock line 3 times to start the next pressure reading
      for (char i = 0; i < 3; i++) {
        digitalWrite(sckPin, HIGH);
        digitalWrite(sckPin, LOW);
      }
      long reading = long(result);
      /*CHECK SENSOR DIRECTION INHALE OR EXHALE*/
      if (abs(baseline - reading) > THRESHOLD) {
        if (reading > baseline) {  //blow = higher values
          dir = 1;
        } else if (reading < baseline) {  //suck = lower values
          dir = -1;
        }
      } else {
        dir = 0;
      }

      // Serial.print(" reading: ");
      // Serial.print(reading);
      // Serial.print(" reading diff: ");
      // Serial.print(abs(baseline - reading));
      // Serial.print(" THRESHOLD; ");
      // Serial.print(THRESHOLD);
      // Serial.print(" baseline: ");
      // Serial.println(baseline);
      creadings = readButtons();

      if (dir == 0) {
        if (playing) {
          for (int i = -10; i < 36; i++) {
            BLEMidiServer.noteOff(channel, pnote + i, 120);
            //delay(2);
          }
          // BLEMidiServer.controlChange(channel, 123, 120);
          playing = false;
          Serial.println("all off");
        }
        pnote = -1;
        preadings = -1;
      }  // BLOW //
      else if (dir == 1 && !playing) {
        if (preadings != creadings) {
          switch (creadings) {
            case 0:  // open  -> C
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              Serial.println("C");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              pnote = rootNote;
              preadings = creadings;
              playing = true;
              break;
            case 1:  // TL  -> G
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[4], 120);
              Serial.println("G");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              pnote = rootNote + 7;
              preadings = creadings;
              playing = true;
              break;
            case 2:  // BL   -> A
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[4], 120);
              Serial.println("A");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 9;
              preadings = creadings;
              playing = true;
              break;
            case 3:  //  TL BL  -> Bb / A#
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[4], 120);
              Serial.println("A# Bb");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 10;
              preadings = creadings;
              playing = true;
              break;
            case 4:  //  BR -> E
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[4], 120);
              Serial.println("E");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 4;
              preadings = creadings;
              playing = true;
              break;
            case 5:  // TL BR -> Eb  / D#
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[4], 120);
              Serial.println("D# Eb");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 3;
              preadings = creadings;
              playing = true;
              break;
            case 6:  // BL BR -> Db / C#
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[4], 120);
              Serial.println("C# Db");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 1;
              preadings = creadings;
              playing = true;
              break;
            case 7:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan1");
              break;
            case 8:  // TR  -> D
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[4], 120);
              Serial.println("D");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 2;
              preadings = creadings;
              playing = true;
              break;
            case 9:  // TL TR F
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              Serial.println("F");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 5;
              preadings = creadings;
              playing = true;
              break;
            case 10:  // BL TR  F# / Gb
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[4], 120);
              Serial.println("F# Gb");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 6;
              preadings = creadings;
              playing = true;
              break;
            case 11:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan3");
              break;
            case 12:  //  TR BR -> B
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[4], 120);
              Serial.println("B");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 11;
              preadings = creadings;
              playing = true;
              break;
            case 13:  // no
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("F nan2");
              break;
            case 14:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan4");
              break;
            case 15:  //    TL TR BL BR -> Ab / G#
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[4], 120);
              Serial.println("G# Ab");
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              // BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              // Serial.print(pnote);
              // Serial.println(" off");
              pnote = rootNote + 8;
              preadings = creadings;
              playing = true;
              break;
          }
        }
      } else if (dir == 1 && playing) {
        if (preadings != creadings) {
          switch (creadings) {
            case 0:  // open  -> C
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              Serial.println("C");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              pnote = rootNote;
              preadings = creadings;
              playing = true;
              break;
            case 1:  // TL  -> G
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[4], 120);
              Serial.println("G");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              pnote = rootNote + 7;
              preadings = creadings;
              playing = true;
              break;
            case 2:  // BL   -> A
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[4], 120);
              Serial.println("A");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 9;
              preadings = creadings;
              playing = true;
              break;
            case 3:  //  TL BL  -> Bb / A#
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[4], 120);
              Serial.println("A# Bb");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 10;
              preadings = creadings;
              playing = true;
              break;
            case 4:  //  BR -> E
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[4], 120);
              Serial.println("E");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 4;
              preadings = creadings;
              playing = true;
              break;
            case 5:  // TL BR -> Eb  / D#
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[4], 120);
              Serial.println("D# Eb");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 3;
              preadings = creadings;
              playing = true;
              break;
            case 6:  // BL BR -> Db / C#
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[4], 120);
              Serial.println("C# Db");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 1;
              preadings = creadings;
              playing = true;
              break;
            case 7:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan1");
              break;
            case 8:  // TR  -> D
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[4], 120);
              Serial.println("D");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 2;
              preadings = creadings;
              playing = true;
              break;
            case 9:  // TL TR F
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              Serial.println("F");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 5;
              preadings = creadings;
              playing = true;
              break;
            case 10:  // BL TR  F# / Gb
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[4], 120);
              Serial.println("F# Gb");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 6;
              preadings = creadings;
              playing = true;
              break;
            case 11:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan3");
              break;
            case 12:  //  TR BR -> B
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[4], 120);
              Serial.println("B");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 11;
              preadings = creadings;
              playing = true;
              break;
            case 13:  // no
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("F nan2");
              break;
            case 14:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan4");
              break;
            case 15:  //    TL TR BL BR -> Ab / G#
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[4], 120);
              Serial.println("G# Ab");
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 8;
              preadings = creadings;
              playing = true;
              break;
          }
        }
      }  //SUCK//
      else if (dir == -1 && !playing) {
        if (preadings != creadings) {
          switch (creadings) {
            case 0:  // open  -> Am
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[4], 120);
              Serial.println("Am");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              preadings = creadings;
              pnote = rootNote - 3;
              playing = true;
              break;
            case 1:  // TL  -> Em
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[4], 120);
              Serial.println("Em");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              pnote = rootNote + 4;
              preadings = creadings;
              playing = true;
              break;
            case 2:  // BL   -> F#m
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[4], 120);
              Serial.println("F#m / Gbm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 6;
              preadings = creadings;
              playing = true;
              break;
            case 3:  //  TL BL  -> Gm
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[4], 120);
              Serial.println("Gm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 5;
              preadings = creadings;
              playing = true;
              break;
            case 4:  //  BR -> C#m
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[4], 120);
              Serial.println("C#m");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 1;
              preadings = creadings;
              playing = true;
              break;
            case 5:  // TL BR -> Cm
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[4], 120);
              Serial.println("Cm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote;
              preadings = creadings;
              playing = true;
              break;
            case 6:  // BL BR -> Bbm A#m
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[4], 120);
              Serial.println("Bbm / A#m");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 2;
              preadings = creadings;
              playing = true;
              break;
            case 7:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan1");
              break;
            case 8:  // TR  -> Bm
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[4], 120);
              Serial.println("Bm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 1;
              preadings = creadings;
              playing = true;
              break;
            case 9:  // TL TR Dm
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[4], 120);
              Serial.println("Dm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 2;
              preadings = creadings;
              playing = true;
              break;
            case 10:  // BL TR D#m / Ebm
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[4], 120);
              Serial.println("D#m / Ebm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 3;
              preadings = creadings;
              playing = true;
              break;
            case 11:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan3");
              break;
            case 12:  //  TR BR ->  G#m Abm
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[4], 120);
              Serial.println("G#m Abm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 4;
              preadings = creadings;
              playing = true;
              break;
            case 13:  // no
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan2");
              break;
            case 14:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan4");
              break;
            case 15:  //    TL TR BL BR -> Fm
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[4], 120);
              Serial.println("Fm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 5;
              preadings = creadings;
              playing = true;
              break;
          }
        }
      } else if (dir == -1 && playing) {
        if (preadings != creadings) {
          switch (creadings) {
            case 0:  // open  -> Am
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[4], 120);
              Serial.println("Am");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              preadings = creadings;
              pnote = rootNote - 3;
              playing = true;
              break;
            case 1:  // TL  -> Em
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMinor[4], 120);
              Serial.println("Em");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              pnote = rootNote + 4;
              preadings = creadings;
              playing = true;
              break;
            case 2:  // BL   -> F#m
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMinor[4], 120);
              Serial.println("F#m / Gbm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 6;
              preadings = creadings;
              playing = true;
              break;
            case 3:  //  TL BL  -> Gm
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 5) + intervalMinor[4], 120);
              Serial.println("Gm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 5;
              preadings = creadings;
              playing = true;
              break;
            case 4:  //  BR -> C#m
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMinor[4], 120);
              Serial.println("C#m");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 1;
              preadings = creadings;
              playing = true;
              break;
            case 5:  // TL BR -> Cm
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, rootNote + intervalMinor[4], 120);
              Serial.println("Cm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote;
              preadings = creadings;
              playing = true;
              break;
            case 6:  // BL BR -> Bbm A#m
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 2) + intervalMinor[4], 120);
              Serial.println("Bbm / A#m");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 2;
              preadings = creadings;
              playing = true;
              break;
            case 7:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan1");
              break;
            case 8:  // TR  -> Bm
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 1) + intervalMinor[4], 120);
              Serial.println("Bm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 1;
              preadings = creadings;
              playing = true;
              break;
            case 9:  // TL TR Dm
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMinor[4], 120);
              Serial.println("Dm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 2;
              preadings = creadings;
              playing = true;
              break;
            case 10:  // BL TR D#m / Ebm
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMinor[4], 120);
              Serial.println("D#m / Ebm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 3;
              preadings = creadings;
              playing = true;
              break;
            case 11:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan3");
              break;
            case 12:  //  TR BR ->  G#m Abm
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote - 4) + intervalMinor[4], 120);
              Serial.println("G#m Abm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote - 4;
              preadings = creadings;
              playing = true;
              break;
            case 13:  // no
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan2");
              break;
            case 14:  // not currently used. // note off?
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
              // BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
              preadings = creadings;
              Serial.println("nan4");
              break;
            case 15:  //    TL TR BL BR -> Fm
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[0], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[2], 120);
              BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMinor[4], 120);
              Serial.println("Fm");
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[0], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[2], 120);
              BLEMidiServer.noteOff(channel, pnote + intervalMinor[4], 120);
              Serial.print(pnote);
              Serial.println(" off");
              pnote = rootNote + 5;
              preadings = creadings;
              playing = true;
              break;
          }
        }
      }
    }
  } else {
    // blink led when not connected
    if (millis() - pLed > ledInterval) {
      ledOn = !ledOn;
      pLed = millis();
    }
    digitalWrite(ledPin, ledOn);
  }
}

/// read buttons function
byte readButtons() {
  byte thisbyte = B0000;
  cReset = digitalRead(resetButtonPin);
  for (int i = 0; i < numButtons; i++) {
    states[i] = !digitalRead(buttonPins[i]);
    thisbyte = thisbyte + (states[i] << i);
  }
  return thisbyte;
}