/* 
Only works with NimBLE-Arduino v1.4.3
esp32 3.3.0 board defs (last checked)
*/

#include <Arduino.h>
#include <BLEMidi.h>
#include "mappings.h"
/*breathe sensor*/
#define sckPin 7
#define dataPin 8
// #define THRESHOLD 1000000
#define THRESHOLD 80000  // adjust as needed
long preading = 0;
long baseline;
int dir = 0;
int pdir = 0;

byte creadings = B1111;
byte preadings = B1111;

int rootNote = 60;  // middle C
int intervalMajor[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
int intervalMinor[8] = { 0, 2, 3, 5, 7, 9, 11, 12 };

// will change later///
int pnotes[6] = {};
int notes[6] = {};

byte result = B0000;  // button results as bitmask



/*buttons*/
int pstates[4] = {};
int states[4] = {};
int numButtons = 4;
int buttonPins[4] = { 5, 6, 43, 44 };
int resetButtonPin = 4;

// reset button is pin 4 on bb //
// led is on pin 3 //
int ledPin = 3;

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
  // preading = reading;
  baseline = reading;
  creadings = readButtons();
  preadings = creadings;
}

void loop() {
  if (BLEMidiServer.isConnected()) {  // If we've got a connection, we send an A4 during one second, at full velocity (127)
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
    // //Serial.println(preading-reading);
    // Serial.print(baseline);
    // Serial.print(" , ");
    // Serial.print(reading);
    // Serial.print(" , ");
    // Serial.print(preading - reading);
    // int change = preading - reading;
    // Serial.print(" , ");

    /*CHECK SENSOR DIRECTION INHALE OR EXHALE*/
    if (abs(baseline - reading) > THRESHOLD) {
      if (reading > baseline) {  //blow
        dir = 1;
      } else if (reading < baseline) {  //suck
        dir = -1;
      }
    } else {
      dir = 0;
    }

    creadings = readButtons();

    // Serial.println(readButtons());

    //  if (dir != 0 && creadings != preadings) {
    if (dir != 0) {
      // different conditions for buttons vs opennnn...... ught


      if (dir == 1) {
        switch (creadings) {
          case 0:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            Serial.print(reading);
            Serial.print("  ");
            Serial.print(dir);
            Serial.print("  ");
            Serial.print(creadings, BIN);
            Serial.print("  ");
            Serial.println(creadings, DEC);
            break;
          case 1:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 2:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 3:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 4:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 5:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 6:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 7:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 8:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 9:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 10:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 11:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 12:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 13:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 14:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 15:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          case 16:
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[0], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[1], 120);
            BLEMidiServer.noteOn(0, rootNote + intervalMajor[2], 120);
            break;
          default:
            // for loop everything off?
            BLEMidiServer.noteOff(0, rootNote + intervalMajor[0], 120);
            break;
        }


      } else if (dir == -1) {
        if (creadings == 0) {
          BLEMidiServer.noteOn(0, rootNote + intervalMinor[0], 120);
          BLEMidiServer.noteOn(0, rootNote + intervalMinor[1], 120);
          BLEMidiServer.noteOn(0, rootNote + intervalMinor[2], 120);
          Serial.print(reading);
          Serial.print("  ");
          Serial.print(dir);
          Serial.print("  ");
          Serial.print(creadings, BIN);
          Serial.print("  ");
          Serial.println(creadings, DEC);
        }
      }
    } else if (dir == 0 && dir != pdir) {
      for (int i = 0; i < 8; i++) {
        BLEMidiServer.noteOff(0, rootNote + intervalMajor[i], 120);
        BLEMidiServer.noteOff(0, rootNote + intervalMinor[i], 120);
      }
      Serial.print(reading);
      Serial.print("  ");
      Serial.print(dir);
      Serial.print("  ");
      Serial.print(creadings, BIN);
      Serial.print("  ");
      Serial.println(creadings, DEC);
    }




    /*new stuff*/
    ///int dir = preading - reading;
    // for (int i = 0; i < numButtons; i++) {
    //   states[i] = digitalRead(pins[i]);

    //   if (states[i] != pstates[i] && dir != 0) {
    //     if (i <= 4) {
    //       if (states[i] == 0 && dir > 0) {
    //         //notes[i] = scale[i];
    //         pnotes[i] = notes[i];
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMajor[0], 120);
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMajor[2], 120);
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMajor[4], 120);
    //         BLEMidiServer.noteOn(0, notes[i] - 12, 120);

    //       } else if (states[i] == 0 && dir < 0) {
    //         // notes[i] = scale[i] + 5;
    //         pnotes[i] = notes[i];
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMinor[0], 120);
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMinor[2], 120);
    //         BLEMidiServer.noteOn(0, notes[i] + intervalMinor[4], 120);
    //         BLEMidiServer.noteOn(0, notes[i] - 12, 120);
    //       }
    //       //preading = reading;

    //       if (states[i] == 1) {
    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMajor[0], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMajor[2], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMajor[4], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] - 12, 120);

    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMinor[0], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMinor[2], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] + intervalMinor[4], 120);
    //         BLEMidiServer.noteOff(0, pnotes[i] - 12, 120);
    //       }
    //     } else {
    //       if (states[i] == 0 && dir > 0 || states[i] == 0 && dir < 0) {
    //         ESP.restart();
    //       }
    //     }
    //     pstates[i] = states[i];
    //   }
    // }

    //update sensor readings
    //preading = reading;

    preadings = creadings;
    pdir = dir;
  } else {
    digitalWrite(ledPin, LOW);
  }
}



byte readButtons() {
  byte thisbyte = B0000;
  for (int i = 0; i < numButtons; i++) {
    states[i] = !digitalRead(buttonPins[i]);
    thisbyte = thisbyte + (states[i] << i);
  }
  return thisbyte;
}