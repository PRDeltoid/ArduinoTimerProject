#include <arduino-timer.h>
#include <Wire.h> // Enable this line if using Arduino Uno, Mega, etc.
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

const int ALARM_LENGTH = 5; // 5 second alarm at the end of the timer

int timer = 60;

int buttonPin = 16; 

bool shouldBlink = false; // Flag determining if we should blink the display when the timer reaches blinkTime seconds left
int blinkTime = 30; // The amount of seconds left in the timer when the display should begin blinking
int blinkRate = 1; // Speed of display blinkage

int alarmPin = 8;
int alarmTone = 523; // TODO: Make a tone tester and find a nice tone for the alarm
int alarmToneLength = 300; // 300 ms chirp 
bool makeAlarm = false;

auto actionTimer = timer_create_default(); // create a timer with default settings
auto alarmTimer = timer_create_default(); // create a timer with default settings

Adafruit_7segment matrix = Adafruit_7segment();

void setup() {
  pinMode(buttonPin, INPUT);
  matrix.begin(0x70);
  matrix.setBrightness(5);
  matrix.drawColon(true);
  actionTimer.every(1000, dec_timer);
  alarmTimer.every(1000, make_tone);

}

int lastButtonState = 0;     // previous state of the button
void loop() {
  
  int buttonState = digitalRead(buttonPin);
  if (buttonState != lastButtonState) {
    // Check for button press
    if (buttonState == LOW)
    {
      // Add 30 seconds to timer
      timer += 30;
      write_timer();
    }
    lastButtonState = buttonState;
  }
  
  actionTimer.tick();
  alarmTimer.tick();
}

// Do some checks, subtract 1 second from our timer, and then display it
bool dec_timer(void *) {
  if(timer == 0) {
    // Exit early if timer has finished
    // and stop blinking
    matrix.blinkRate(0);
    make_alarm(ALARM_LENGTH);

    return false; // do not repeat once timer hits zero
  }
  
  //Serial.print("Timer value: ");
  //Serial.println(timer);
  timer -= 1; // dec timer
  write_timer();

  return true; // timer isn't done, repeat
}


// Make an alarm noise "length" amount of times
int alarmLength = ALARM_LENGTH;
void make_alarm(int length) {
  alarmLength = length;
  makeAlarm = true;
}

// Make an alarm tone
// Tone will only play if makeAlarm is true
bool make_tone(void *) {
  // Exit early if we arn't meant to make an alarm
  if(makeAlarm == false) return true;
  // We made a chirp. Minus one from the chirp counter
  alarmLength--;
  // We made all of our chirps. Turn off the tone maker
  if(alarmLength == 0) {
    makeAlarm = false;
  }
  // If we get here, make a noise
  tone(alarmPin, alarmTone, alarmToneLength);
  // Repeat forvever
  return true;
}


// Convert the current timer value (in seconds) into a clock display "MM:SS" and writes it to the display
void write_timer() {
  // If the timer is too high to draw, just write something silly
  if(timer > 3600) {
     write_error();
     return;
  } else {
    // make sure we draw the colon (this is sometimes turned off when displaying "Err" message)
      matrix.drawColon(true);
  }
  
  // Start blinking if only 30 seconds are left
  if(timer < blinkTime && shouldBlink) {
      matrix.blinkRate(blinkRate);
  }
  int minutes = 0;
  int seconds = timer;
  
  if(timer > 60) {
    minutes = timer / 60;
    seconds = timer % 60;
  }

  // loc 0 is furthest left (highest value)
  // loc 2 is the colon dots
  // loc 4 is further right (lower value)
  // Extract the first and second digits from both values 
  matrix.writeDigitNum(0, minutes / 10);
  matrix.writeDigitNum(1, minutes % 10);
  matrix.writeDigitNum(3, seconds / 10);
  matrix.writeDigitNum(4, seconds % 10);
  matrix.writeDisplay();
}

// Writes "Err" to the display
void write_error() {
  matrix.drawColon(false);
  matrix.writeDigitRaw(0, B01111001); //E
  matrix.writeDigitRaw(1, B01010000); //r
  matrix.writeDigitRaw(3, B01010000); //r
  matrix.writeDigitRaw(4, B00000000); // Nothing
  matrix.writeDisplay();   
}

// Blanks out the display
void write_blank() {
  matrix.writeDigitRaw(0, B00000000); // Nothing
  matrix.writeDigitRaw(1, B00000000); // Nothing
  matrix.writeDigitRaw(3, B00000000); // Nothing
  matrix.writeDigitRaw(4, B00000000); // Nothing
}
