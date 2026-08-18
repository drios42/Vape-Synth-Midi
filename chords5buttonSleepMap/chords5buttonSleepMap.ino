/* 
Only works with NimBLE-Arduino v1.4.3
esp32 3.3.0 board defs (last checked)
*/
#include <Arduino.h>
#include <BLEMidi.h>
#include "mappings.h"
#include "driver/rtc_io.h"

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
#define USE_EXT0_WAKEUP 0  // enable external
#define WAKEUP_GPIO GPIO_NUM_4
RTC_DATA_ATTR int bootCount = 0;

/*breathe sensor comm pins*/
#define sckPin 7
#define dataPin 8
// #define a sensor THRESHOLD. Example uses 1000000
long THRESHOLD = 900000;  // adjust as needed
long bTHRESHOLD = 10000;
bool calibrated = false;
long ptime = 0;
int interval = 10000;

bool playing = false;
// for drift and threshold calculations
int driftcounter = 0;
int sum = 0;
// variables for state changes and neutral states
long pbreathe = 0;
long baseline;
long baselineavg = 0;

int dir = 0;
int pdir = 0;

int pnote = -1;
int note = 0;

long sensorHIGH = 0;
long sensorLOW = 100000000;
long threshHIGH = 0;
long threshLOW = 100000000;

// variables for the button states as bitmask
byte creadings = B1111;
byte preadings = B1111;

//MIDI SETUP
int rootNote = C2;  // middle C
int channel = 1;

// Major and Minor intervals
int intervalMajor[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
int intervalMinor[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };

// bytemask of 4 button states
byte result = B0000;  // button results as bitmask

/*buttons*/
int pstates[4] = {};
int states[4] = {};
int numButtons = 4;
int buttonPins[4] = { 5, 6, 43, 44 };
int resetButtonPin = 4;
int pReset = 1;
int cReset = 1;

// reset button is pin 4 on bb //
// led is on pin 3 //
int ledPin = 3;

int sleepInterval = 5000;
int pSleepTime = 0;

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
  BLEMidiServer.begin("Basic MIDI device");
  Serial.println("Waiting for connections...");
  //BLEMidiServer.enableDebugging();  // Uncomment if you want to see some debugging output from the library (not much for the server class...)
  pinMode(resetButtonPin, INPUT_PULLUP);
  /*buttons*/
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    // notes[i] = root + circleOfFifths[i];
    // pnotes[i] = notes[i];
    states[i] = digitalRead(buttonPins[i]);
    pstates[i] = digitalRead(buttonPins[i]);
  }
  /*breathe sensor*/
  pinMode(dataPin, INPUT);  // Connect HX710 OUT to Arduino pin 10
  pinMode(sckPin, OUTPUT);  // Connect HX710 SCK to Arduino pin 8

  /*LEDS*/
  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);
  //readings//
  creadings = readButtons();
  preadings = -1;  // button readings
  ptime = millis();
  pReset = digitalRead(4);
  cReset = digitalRead(4);

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
      digitalWrite(3, LOW);
      Serial.println("Going to sleep now");
      delay(2000);
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
      /*
  Serial.print("playing");
        Serial.print(playing);
        Serial.print(" p readings ");
        Serial.print(creadings);
        Serial.print(" creadings ");
        Serial.print(creadings);
        Serial.print(" dir: ");
        Serial.print(dir);
        Serial.println(" C ");
*/

      // if (dir == 1) {                       // if blowing play something
      //   if (creadings == 0 && pdir != 1) {  //c
      //     BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
      //     delay(2);
      //     BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
      //     delay(2);
      //     BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
      //     Serial.println("C");
      //     delay(2);
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 0);
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 0);
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 0);
      //     pnote = rootNote;
      //     preadings = creadings;
      //     playing = true;
      //   }
      //   if (preadings != creadings) {
      //     switch (creadings) {
      //       case 1:  // TL  -> G
      //         BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 7) + intervalMajor[4], 120);
      //         Serial.println("G");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 7;
      //         preadings = creadings;
      //         break;
      //       case 2:  // BL   -> A
      //         BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 9) + intervalMajor[4], 120);
      //         Serial.println("A");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 9;
      //         preadings = creadings;
      //         break;
      //       case 3:  //  TL BL  -> Bb / A#
      //         BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 10) + intervalMajor[4], 120);
      //         Serial.println("A# Bb");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 10;
      //         preadings = creadings;
      //         break;
      //       case 4:  //  BR -> E
      //         BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 4) + intervalMajor[4], 120);
      //         Serial.println("E");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 4;
      //         preadings = creadings;
      //         break;
      //       case 5:  // TL BR -> Eb  / D#
      //         BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 3) + intervalMajor[4], 120);
      //         Serial.println("D# Eb");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 3;
      //         preadings = creadings;
      //         break;
      //       case 6:  // BL BR -> Db / C#
      //         BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 1) + intervalMajor[4], 120);
      //         Serial.println("C# Db");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 1;
      //         preadings = creadings;
      //         break;
      //       case 7:  // not currently used. // note off?
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
      //         preadings = creadings;
      //         Serial.println("nan1");
      //         break;
      //       case 8:  // TR  -> D
      //         BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 2) + intervalMajor[4], 120);
      //         Serial.println("D");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 2;
      //         preadings = creadings;
      //         break;
      //       case 9:  // TL TR F
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
      //         Serial.println("F");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 5;
      //         preadings = creadings;
      //         break;
      //       case 10:  // BL TR  F# / Gb
      //         BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 6) + intervalMajor[4], 120);
      //         Serial.println("F# Gb");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 6;
      //         preadings = creadings;
      //         break;
      //       case 11:  // not currently used. // note off?
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
      //         preadings = creadings;
      //         Serial.println("nan3");
      //         break;
      //       case 12:  //  TR BR -> B
      //         BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 11) + intervalMajor[4], 120);
      //         Serial.println("B");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 11;
      //         preadings = creadings;
      //         break;
      //       case 13:  // no
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 5) + intervalMajor[4], 120);
      //         preadings = creadings;
      //         Serial.println("F nan2");
      //         break;
      //       case 14:  // not currently used. // note off?
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, rootNote + intervalMajor[4], 120);
      //         preadings = creadings;
      //         Serial.println("nan4");
      //         break;
      //       case 15:  //    TL TR BL BR -> Ab / G#
      //         BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[0], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[2], 120);
      //         BLEMidiServer.noteOn(channel, (rootNote + 8) + intervalMajor[4], 120);
      //         Serial.println("G# Ab");
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //         BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //         Serial.print(pnote);
      //         Serial.println(" off");
      //         pnote = rootNote + 8;
      //         preadings = creadings;
      //         break;
      //     }
      //     playing = true;
      //     // preadings = creadings;
      //     pdir = dir;
      //   }

      // } else if (dir == -1) {
      //   if (creadings == 0 && pdir != -1) {
      //     BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[0], 120);
      //     delay(2);
      //     BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[2], 120);
      //     delay(2);
      //     BLEMidiServer.noteOn(channel, (rootNote - 3) + intervalMinor[4], 120);
      //     delay(2);
      //     Serial.println("A minor");
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[0], 120);
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[2], 120);
      //     BLEMidiServer.noteOff(channel, pnote + intervalMajor[4], 120);
      //     pnote = rootNote - 3;
      //     preadings = creadings;
      //     playing = true;
      //   }
      //   pdir = dir;
      // } else if (dir == 0 && playing == true) {
      //   for (int i = -12; i < 48; i++) {
      //     BLEMidiServer.noteOff(channel, rootNote + i, 0);
      //   }
      //   playing = false;
      //   preadings = creadings;
      //   pdir = 0;
      //   Serial.println("all notes off");
      // }
    }
  } else {
    digitalWrite(ledPin, LOW);
  }
}

byte readButtons() {
  byte thisbyte = B0000;
  cReset = digitalRead(resetButtonPin);
  for (int i = 0; i < numButtons; i++) {
    states[i] = !digitalRead(buttonPins[i]);
    thisbyte = thisbyte + (states[i] << i);
  }
  return thisbyte;
}
// Serial.print(pdir);
// Serial.print(" ");
// Serial.print(dir);
// Serial.print(" ");
// Serial.print(abs(baseline - reading) > THRESHOLD);
// Serial.print(" ");
// Serial.print(THRESHOLD);
// Serial.print(" ");
// Serial.print(reading);
// Serial.print(" ");
// Serial.print(cReset);
// Serial.print(" ");
// Serial.print(pReset);
// Serial.print(" ");
// Serial.println(readButtons());
// Serial.println(dir);