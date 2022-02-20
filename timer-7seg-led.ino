#include <arduino-timer.h>
#include <Adafruit_GFX.h>
#include <AceButton.h>
#include <Adafruit_LEDBackpack.h>
#include "pitches.h"

using namespace ace_button;

////////////
// SETTINGS
////////////

// Timer values
const int ADD_TIME_SECONDS = 5 * 60; // 5 minutes
const int TIMER1_SECONDS = 10 * 60; // 10 minutes
const int TIMER2_SECONDS = 45 * 60; // 45 minutes

// Pin values
const int ALARM_PIN = 6;
const int STOP_BUTTON_PIN = 1;
const int PAUSE_BUTTON_PIN = 0;
const int ADD_TIME_BUTTON_PIN = 8; 
const int START_TIMER1_PIN = 3;
const int START_TIMER2_PIN = 2;

// Other settings
const int BRIGHTNESS = 3;   // Valid values: 1-10
const int BLINK_RATE = 1;   // Valid values: 0-3
const int BLINK_TIME = 30;  // The amount of seconds left in the timer when the display should begin blinking
const int ALARM_LENGTH = 5; // 5 second alarm at the end of the timer
const int ALARM_TONE = NOTE_C5; // TODO: Make a tone tester and find a nice tone for the alarm
const int ALARM_TONE_LENGTH = 300; // 300 ms chirp 

// MEMBERS
int timer = 0; // The all-mighty timer (value in seconds)

// Tracks the amount of alarm chirps that have been made. 
// Initialized to ALARM_LENGTH so our make_tone function doesn't have to initialize it the first time the alarm is called
int alarmLength = ALARM_LENGTH; 

bool shouldBlink = false;   // Flag determining if we should blink the display when the timer reaches blinkTime seconds left
bool isBlinking = false;    // Flag tracking if we are already blinking
bool makeAlarm = false;     // Flag determining if we should make a tone to indicate the timer is done
bool timerRunning = false;  // Flag determining if there is currently a timer running
bool paused = false;        // Flag determining if the timer is set but paused

auto actionTimer = timer_create_default();  // Main "timer" timer
auto alarmTimer = timer_create_default();   // Timer for the alarm. Shorter period = faster alarm

Adafruit_7segment matrix = Adafruit_7segment(); // 7-seg LED driver

// Button setup
void handleStopButton(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/);
void handlePauseButton(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/);
void handleAddTimeButton(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/);
void handleTimer1Button(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/);
void handleTimer2Button(AceButton* /*button*/, uint8_t eventType, uint8_t /*buttonState*/);

AceButton stopButton(STOP_BUTTON_PIN);
AceButton pauseButton(PAUSE_BUTTON_PIN);
AceButton addTimeButton(ADD_TIME_BUTTON_PIN);
AceButton timer1Button(START_TIMER1_PIN);
AceButton timer2Button(START_TIMER2_PIN);

void setup() {
  // Pin setup
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ADD_TIME_BUTTON_PIN, INPUT_PULLUP);
  pinMode(START_TIMER1_PIN, INPUT_PULLUP);  
  pinMode(START_TIMER2_PIN, INPUT_PULLUP);

  // Button event handler binding
  ButtonConfig* config = ButtonConfig::getSystemButtonConfig();
  config->setEventHandler(buttonHandler);

  // LED setup
  matrix.begin(0x70);
  matrix.setBrightness(BRIGHTNESS);
  matrix.drawColon(true);

  // Timer setup
  actionTimer.every(1000, dec_timer);
  alarmTimer.every(1000, make_tone);
}

void loop() {
  // Check if any buttons were pressed
  stopButton.check();  
  pauseButton.check();
  addTimeButton.check();
  timer1Button.check();
  timer2Button.check();
  
  // Tick timers
  actionTimer.tick();
  alarmTimer.tick();
}

void buttonHandler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  // Exit early if the event isn't a keypress
  if(eventType != AceButton::kEventPressed) { return; }
  switch (button->getPin()) {
    case STOP_BUTTON_PIN:
      Serial.println("Stop button pressed");
      stop_timer();
      break;
     case PAUSE_BUTTON_PIN:
      Serial.println("Pause button pressed");
      pause_or_play();
      break;
     case ADD_TIME_BUTTON_PIN:
      Serial.println("Add Time button pressed");
      add_time(ADD_TIME_SECONDS);
      break;
     case START_TIMER1_PIN:
      Serial.println("Timer 1 button pressed");
      start_timer(TIMER1_SECONDS);
      break;
     case START_TIMER2_PIN:
      Serial.println("Timer 2 button pressed");
      start_timer(TIMER2_SECONDS);
      break;
  }
}

void add_time(int timeSeconds) {
  timer += timeSeconds;
  if(timerRunning == false) {
    timerRunning = true;
    matrix.drawColon(true);
  }
}

void start_timer(int timerSeconds) {
  // Add time to the timer
  timer = timerSeconds;
  // If the timer was stopped, start it again
  if(timerRunning == false) {
    timerRunning = true;
    matrix.drawColon(true);
  }
}

void stop_timer() {
  timer = 0;
  timerRunning = false;
  stop_blink();
  write_blank();
}

void pause_or_play() {
  paused = !paused;
  if(paused) {
    start_blink();
  } else {
    stop_blink();
  }
}

// Do some checks, subtract 1 second from our timer, and then display it
bool dec_timer(void *) {
  // Exit early if the alarm is not running (paused or done)
  if(timerRunning == false || paused) return true;
  if(timer == 0) {
    // Exit early if timer has finished
    // and stop blinking
    make_alarm();
    stop_blink();
    write_blank();
    timerRunning = false;
    return true;
  }
  timer -= 1; // dec timer
      
  // Start blinking if only BLINK_TIME seconds are left
  if(shouldBlink && timer < BLINK_TIME) {
      start_blink();
  }
  
  write_timer();
  return true;
}

/////
// ALARM HELPERS
////

// Make an alarm tone
// Tone will only play if makeAlarm is true
// Called every second by alarmTimer in loop()
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
  tone(ALARM_PIN, ALARM_TONE, ALARM_TONE_LENGTH);
  // Repeat forvever
  return true;
}

// Tell make_tone to start making an alarm
// The alarm will sound ALARM_LENGTH amount of times
void make_alarm() {
  alarmLength = ALARM_LENGTH;
  makeAlarm = true;
}

////
// BLINK HELPERS
////

void start_blink() {
  if(isBlinking == false) {
    matrix.blinkRate(BLINK_RATE);
    isBlinking = true;
  }
}

void stop_blink() {
  if(isBlinking) {
    matrix.blinkRate(0);
    isBlinking = false;
  }
}

////
// 7-SEG LED SCREEN HELPERS
////

// Convert the current timer value (in seconds) into a clock display
void write_timer() {
  // If the timer is too high to draw as MM:SS, draw as HH:MM instead
  if(timer > 3600) {    
     write_hour_minutes(timer);
  } else {
    write_minutes_seconds(timer);
  }
}

// Writes minutes/seconds as MM:SS
void write_minutes_seconds(int seconds) {
  int minutes = 0;
  
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

// Writes hours/minutes as HH:MM
void write_hour_minutes(int seconds) {
  int hours = seconds / (60 * 60);
  int minutes = seconds / (60) % 60;
  matrix.writeDigitNum(0, hours / 10);
  matrix.writeDigitNum(1, hours % 10);
  matrix.writeDigitNum(3, minutes / 10);
  matrix.writeDigitNum(4, minutes % 10);
  matrix.writeDisplay();
}

// Writes hours/minutes as h.m'hr' where h is the number of hours and m is the single-decimal percision percent of minutes in an hour
void write_hour_minutes2(int seconds) {
  // Show error if hours > 10 (we cannot display this in a 4 character array if we want to include Hr
  if(seconds > 36000) {
    write_error();
    return;
  }
  
  matrix.drawColon(false);
  int hours = seconds / (60 * 60);
  int minutes = seconds / (60) % 60;
  matrix.writeDigitNum(0, hours % 10, true); // H.
  matrix.writeDigitNum(1, 60 / minutes % 10); // M
  matrix.writeDigitRaw(3, B01110100); //h
  matrix.writeDigitRaw(4, B01010000); //r
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
  matrix.drawColon(false);
  matrix.writeDisplay();   
}
