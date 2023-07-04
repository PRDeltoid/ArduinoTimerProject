#include <ArduinoLowPower.h>
#include <arduino-timer.h>
#include <AceButton.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include "pitches.h"

//#define DEBUG

// Only print serial statements when DEBUG flag is defined
#ifdef DEBUG
  #define DEBUG_SERIAL_BEGIN(x) Serial.begin(x)
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTF(args...) Serial.printf(args)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_DELAY(x) delay(x)
  #define DEBUG_FLUSH() Serial.flush()
  #define DEBUG_END() Serial.end()
  #define DEBUG_WAIT_FOR_SERIAL() while(!Serial) delay(10);
#else
  #define DEBUG_SERIAL_BEGIN(x)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTF(args...)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_DELAY(x)
  #define DEBUG_FLUSH()
  #define DEBUG_END()
  #define DEBUG_WAIT_FOR_SERIAL()
#endif

using namespace ace_button;

struct Note {
  int Note;
  int Duration;
  int PauseAfter;
};

class TonePlayer {
  private:
    int currNote = 0;
    int currPause = 0;
    int numNotes = 0;
    int alarmPin;
    volatile bool isRunning = false;
    const Note* notes;

  public:
    TonePlayer(const Note melodyNotes[], int numNotes, int alarmPin) {
      this->notes = melodyNotes;
      this->numNotes = numNotes;
      this->alarmPin = alarmPin;
    }

    bool isPlaying() {
      return isRunning;
    }
  
    void tick(int ticks) {
      // Exit early if the tone player is not running
      if(isRunning == false or ticks < 1) { return; }
      
      currPause -= ticks;
      
      if(currPause < 0) {
        if(currNote >= numNotes) {
          stop();
          DEBUG_PRINTLN("Tone Player melody completed");
          return;
        }
        DEBUG_PRINTF("Playing note # %d (%d)\n", currNote, notes[currNote].Note);
        tone(alarmPin, notes[currNote].Note, notes[currNote].Duration);

        currPause = notes[currNote].PauseAfter;
        currNote++;
      }    
    }
  
    void stop() {
      isRunning = false;
    }
    
    void activate() {
      DEBUG_PRINTF("Playing melody with %d notes", numNotes);
      isRunning = true;
      currNote = 0;
    }
};

enum State {
  Idle,
  StopwatchRunning,
  TimerRunning,
};

volatile State CurrentState = Idle;

////////////
// SETTINGS
////////////

// Alarm notes
const Note notes[] = {
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 320 },
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 240 },
  { NOTE_C7, 200, 500 },
};

const int numNotes = sizeof(notes)/sizeof(Note);
 
// Timer values
const int ADD_TIME_SECONDS = 5 * 60;  // 5 minutes
const int TIMER1_SECONDS = 10 * 60;   // 10 minutes
const int TIMER2_SECONDS = 45 * 60;   // 45 minutes

// Pin values
const int ALARM_PIN = 6;
const int STOP_BUTTON_PIN = 1;
const int PAUSE_BUTTON_PIN = 0;
const int ADD_TIME_BUTTON_PIN = 7; 
const int START_TIMER1_PIN = 3;
const int START_TIMER2_PIN = 2;

// Other settings
const int BRIGHTNESS = 3;       // Valid values: 1-10
const int BLINK_RATE = 1;       // Valid values: 0-3
const int BLINK_TIME = 30;      // The amount of seconds left in the timer when the display should begin blinking
const bool SHOULD_BLINK = false;// Flag determining if we should blink the display when the timer reaches blinkTime seconds left
const bool SHOULD_BLINK_WHEN_DONE = true; // Flag determinig if the display should blink when the timer reaches 0 seconds left

// MEMBERS
volatile int timer = 0;                // The all-mighty timer (value in seconds) 
bool isBlinking = false;      // Flag tracking if we are already blinking
volatile bool paused = false;          // Flag determining if the timer is set but paused
int prevTicks = 1000;         // Tick tracker for the tone player to piggy back on actionTimer's ticks
bool screenEnabled = true;

volatile bool interruptsEnabled = false;

auto actionTimer = timer_create_default();      // Main "timer" timer
Adafruit_7segment matrix = Adafruit_7segment(); // 7-seg LED driver
TonePlayer tonePlayer(notes, numNotes, ALARM_PIN);            // Passive buzzer tone player

AceButton stopButton(STOP_BUTTON_PIN);
AceButton pauseButton(PAUSE_BUTTON_PIN);
AceButton addTimeButton(ADD_TIME_BUTTON_PIN);
AceButton timer1Button(START_TIMER1_PIN);
AceButton timer2Button(START_TIMER2_PIN);

void setup() {
  DEBUG_SERIAL_BEGIN(9600);
  // Wait for serial connection if we're in debug mode.
  // This prevents us from missing any serial information when debugging but will halt the microcontroller
  // if no USB cable or serial connection is available
  DEBUG_WAIT_FOR_SERIAL();
  DEBUG_PRINTLN("Setup started");

  DEBUG_PRINTLN("Setting up LED screen");
  // LED setup
  // This must be done before any possible calls to matrix are made (such as starting a timer or stopwatch)
  matrix.begin(0x70);
  matrix.setBrightness(BRIGHTNESS);
  matrix.drawColon(true);

  // Pin setup
  DEBUG_PRINTLN("Setting up pins");  
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ADD_TIME_BUTTON_PIN, INPUT_PULLUP);
  pinMode(START_TIMER1_PIN, INPUT_PULLUP);  
  pinMode(START_TIMER2_PIN, INPUT_PULLUP);

  // Attach a wakeup interrupt on all function pins
  DEBUG_PRINTLN("Setting up interrupts");
  // Disable the screen because attaching interrupts runs the 
  // callback immediately which can render weird artifacts on the screen
  screenEnabled = false;

  // Clear interrupt flags on button action pins before assigning them to prevent accidental calling of callback when attaching
  /*EIC->INTFLAG.reg |= 1 << PAUSE_BUTTON_PIN;
  EIC->INTFLAG.reg |= 1 << ADD_TIME_BUTTON_PIN;
  EIC->INTFLAG.reg |= 1 << START_TIMER1_PIN;
  EIC->INTFLAG.reg |= 1 << START_TIMER2_PIN;
  EIC->INTFLAG.reg |= 1 << STOP_BUTTON_PIN;*/
  LowPower.attachInterruptWakeup(PAUSE_BUTTON_PIN, pauseInterruptAction, RISING);
  LowPower.attachInterruptWakeup(ADD_TIME_BUTTON_PIN, addTimeInterruptAction, RISING);
  LowPower.attachInterruptWakeup(START_TIMER1_PIN, startTimerOneInterruptAction, RISING);
  LowPower.attachInterruptWakeup(START_TIMER2_PIN, startTimerTwoInterruptAction, RISING);
  LowPower.attachInterruptWakeup(STOP_BUTTON_PIN, stopInterruptAction, RISING);
    
  // Manually stop timers since attachInterruptWakeup calls 
  // its interrupt function for all attach calls after the first
  stop_timer();

  screenEnabled = true;

  // Button event handler binding
  DEBUG_PRINTLN("Setting up buttons");  
  ButtonConfig* config = ButtonConfig::getSystemButtonConfig();
  config->setEventHandler(buttonHandler);  
  config->setFeature(ButtonConfig::kFeatureClick);
  config->setFeature(ButtonConfig::kFeatureLongPress);

  // Timer setup
  DEBUG_PRINTLN("Setting up timer");  
  actionTimer.every(1000, dec_timer);

  #ifndef DEBUG 
  // Disable builtin LEDs to save power
  // Only do this if we're building for release
  pinMode(PIN_LED, INPUT);
  pinMode(PIN_LED_TXL, INPUT);
  pinMode(PIN_LED_RXL, INPUT);
  #endif

  // Spin two times to indicate to the user that setup is complete 
  // and the device is available for use
  delay(100);
  // 1 rotation (in ms) = speed * 14 * numSpins
  // 150 * 14 * 2 = 4200 (4.2 sec)
  write_spin(2, 150);

  DEBUG_PRINTLN("Setup finished");
}

void loop() {
  // Check if any buttons were pressed
  stopButton.check();  
  pauseButton.check();
  addTimeButton.check();
  timer1Button.check();
  timer2Button.check();

  // Tick timers
  int ticks = actionTimer.tick();
  tonePlayer.tick(prevTicks - ticks);
  prevTicks = ticks;

  switch(CurrentState) {
    case(Idle):
      if(tonePlayer.isPlaying() == false) {
        DEBUG_PRINTLN("Going to sleep");
        DEBUG_FLUSH();
        DEBUG_END();
        interruptsEnabled = true;
        LowPower.sleep();
        interruptsEnabled = false;
        DEBUG_SERIAL_BEGIN(9600);
        DEBUG_WAIT_FOR_SERIAL();
        DEBUG_PRINTLN("Waking up");
      }
      break;
    case(TimerRunning):
    case(StopwatchRunning):
    default:
      // Do nothing in every other case
      break;
  }
}

void stopInterruptAction() {
  if(interruptsEnabled == false) return;

  DEBUG_PRINTLN("Stop interrupt executing");
  stop_blink();
  write_blank();
}

void pauseInterruptAction() {
  if(interruptsEnabled == false) return;

  DEBUG_PRINTLN("Pause (stopwatch) interrupt executing");
  start_stopwatch();
}

void addTimeInterruptAction() {
  if(interruptsEnabled == false) return;

  DEBUG_PRINTLN("Add Time interrupt executing");
  start_timer(ADD_TIME_SECONDS);
}

void startTimerOneInterruptAction() {
  if(interruptsEnabled == false) return;

  DEBUG_PRINTLN("Start Timer 1 interrupt executing");
  start_timer(TIMER1_SECONDS);
}

void startTimerTwoInterruptAction() {
  if(interruptsEnabled == false) return;

  DEBUG_PRINTLN("Start Timer 1 interrupt executing");
  start_timer(TIMER2_SECONDS);
}

void stopButtonAction() {
  DEBUG_PRINTLN("Stop button pressed");
  stop_timer();
}

void pauseButtonAction() {
  DEBUG_PRINTLN("Pause button pressed");
  if(CurrentState == Idle) {
    // If nothing else is happening, the Start/Pause button starts a stopwatch
    start_stopwatch();
  } else {
    // Otherwise, the button pauses or plays the current timer or stopwatch
    pause_or_play();
  }
}

void addTimeButtonAction() {
  DEBUG_PRINTLN("Add Time button pressed");

  // Return early if a stopwatch is currently running to prevent accidental overwriting of the stopwatch with a timer
  if(CurrentState == StopwatchRunning) return;
  start_timer(timer + ADD_TIME_SECONDS);
}

void startTimerOneButtonAction() {
  DEBUG_PRINTLN("Timer 1 button pressed");

  // Return early if a stopwatch is currently running to prevent accidental overwriting of the stopwatch with a timer
  if(CurrentState == StopwatchRunning) return;
  start_timer(TIMER1_SECONDS);
}

void startTimerTwoButtonAction() {
  DEBUG_PRINTLN("Timer 2 button pressed");

  // Return early if a stopwatch is currently running to prevent accidental overwriting of the stopwatch with a timer
  if(CurrentState == StopwatchRunning) return;
  start_timer(TIMER2_SECONDS);
}


void buttonHandler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  if(eventType == AceButton::kEventPressed) {
    DEBUG_PRINTLN("Button Pressed Event");
    switch (button->getPin()) {
      case PAUSE_BUTTON_PIN:
        pauseButtonAction();
        break;
      case ADD_TIME_BUTTON_PIN:
        addTimeButtonAction();
        break;
      case START_TIMER1_PIN:
        startTimerOneButtonAction();
        break;
      case START_TIMER2_PIN:
        startTimerTwoButtonAction();
        break;
      case STOP_BUTTON_PIN:
        // Do nothing if stop is "clicked" (not held) while a timer or stopwatch is running
        // If the timer is idle (and possibly flashing 00:00 at the user), clicking stop will blank the display
        if(CurrentState == StopwatchRunning || CurrentState == TimerRunning) break;
        stopButtonAction();
        break;
    }
  } else if(eventType == AceButton::kEventLongPressed) {
    DEBUG_PRINTLN("Button Long Pressed Event");
    switch (button->getPin()) {
      case STOP_BUTTON_PIN:
        // Require a long press to stop to avoid accidental stopping of timers and stopwatches
        stopButtonAction();
        break;
    }
  }
}

// Attempts to start a timer for timerSeconds
// Will not start a timer if a stopwatch is running
// Returns true on successful timer start
// Returns false otherwise
void start_timer(int timerSeconds) {
  DEBUG_PRINTF("Starting timer for %d seconds\n", timerSeconds);
  timer = timerSeconds;

  // If a blink or tone was playing, stop it before we start the new timer
  stop_blink();
  tonePlayer.stop();

  // If the timer was not running, start it
  if(CurrentState != TimerRunning) {
    CurrentState = TimerRunning;
    matrix.drawColon(true);
  }
  write_timer();
}

void start_stopwatch() {
  DEBUG_PRINTLN("Starting stopwatch");  
  timer = 0;

  // If a blink or tone was playing, stop it before we start the stopwatch
  stop_blink();
  tonePlayer.stop();

  if(CurrentState != StopwatchRunning) {
    CurrentState = StopwatchRunning;
    matrix.drawColon(true);

  }
  write_timer();
}

void stop_timer() {
  DEBUG_PRINTLN("Stopping timer/stopwatch");
  timer = 0;
  CurrentState = Idle;
  paused = false;
  stop_blink();
  write_blank();
}

void pause_or_play() {
  paused = !paused;
  if(paused) {
    DEBUG_PRINTLN("Resuming timer/stopwatch");
    start_blink();
  } else {
    DEBUG_PRINTLN("Pausing timer/stopwatch");
    stop_blink();
  }
}

// Do some checks, subtract 1 second from our timer, and then display it
bool dec_timer(void *) {
  DEBUG_PRINTLN("Dec timer: Start");
  // Don't change the timer if it is currently paused
  if(paused) return true;

  //DEBUG_PRINTLN("Dec timer: Checking CurrentState");
  switch(CurrentState) {
    case(Idle):
      DEBUG_PRINTLN("Dec timer: CurrentState Idle");
      // If idle, bail early as we have nothing to do here
      return true;
    case(TimerRunning):
      DEBUG_PRINTF("Dec timer: CurrentState TimerRunning (Time Left: %d)\n", timer);
      if(timer <= 0) {
        DEBUG_PRINTLN("Timer finished");
        // Play a tone to indicate to the user the timer has finished
        tonePlayer.activate();

        if(SHOULD_BLINK_WHEN_DONE) {
          start_blink();
        } else {
          stop_blink();
          write_blank();
        }

        CurrentState = Idle;
        return true;
      }
      timer -= 1; // dec timer
        
      // Start blinking if only BLINK_TIME seconds are left
      if(SHOULD_BLINK && timer < BLINK_TIME) {
          start_blink();
      }
      break;
    case(StopwatchRunning):
      DEBUG_PRINTLN("Dec timer: CurrentState StopwatchRunning");
      timer += 1;
      break;    
  }
  
  write_timer();
  return true;
}

////
// BLINK HELPERS
////

void start_blink() {
  if(isBlinking == false) {    
    DEBUG_PRINTLN("Starting blink");
    matrix.blinkRate(BLINK_RATE);
    isBlinking = true;
  }
}

void stop_blink() {
  if(isBlinking) {
    DEBUG_PRINTLN("Stopping blink");
    matrix.blinkRate(0);
    isBlinking = false;
  }
}

////
// 7-SEG LED SCREEN HELPERS
////

// Convert the current timer value (in seconds) into a clock display
void write_timer() {
  if(screenEnabled == false) return;
  // If the timer is too high to draw as MM:SS, draw as HH:MM instead
  if(timer > 3600) {    
     write_hour_minutes(timer);
  } else {
    write_minutes_seconds(timer);
  }
}

// Writes minutes/seconds as mm:SS
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

  // Only draw the leading digit if it is non-zero
  int leadingDigit = minutes / 10;
  if(leadingDigit > 0) {
      matrix.writeDigitNum(0, minutes / 10);
  } else {
      matrix.writeDigitRaw(0, B00000000); // Nothing
  }
  matrix.writeDigitNum(1, minutes % 10);
  matrix.writeDigitNum(3, seconds / 10);
  matrix.writeDigitNum(4, seconds % 10);
  matrix.writeDisplay();
}

// Writes hours/minutes as hh:MM
void write_hour_minutes(int seconds) {
  int hours = seconds / (60 * 60);
  int minutes = seconds / (60) % 60;

  // Only draw the leading digit if it is non-zero
  int leadingDigit = hours / 10;
  if(leadingDigit > 0) {
      matrix.writeDigitNum(0, hours / 10);
  } else {
      matrix.writeDigitRaw(0, B00000000); // Nothing
  }
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

void write_spin(const int numSpins, const int speed) {
  // 1 rotation (in ms) = speed * 14 * numSpins
  matrix.drawColon(false);

  for(int i = 0; i < numSpins; i++) {
    matrix.writeDigitRaw(0, 1);
    matrix.writeDigitRaw(1, 0);
    matrix.writeDigitRaw(3, 0);
    matrix.writeDigitRaw(4, 0);
    matrix.writeDisplay();   
    delay(speed);
    matrix.writeDigitRaw(0, 0);
    matrix.writeDigitRaw(1, 1);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(1, 0);
    matrix.writeDigitRaw(3, 1);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(3, 0);
    matrix.writeDigitRaw(4, 1);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(4, B00000010);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(4, B00000100);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(4, B00001000);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(4, 0);
    matrix.writeDigitRaw(3, B00001000);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(3, 0);
    matrix.writeDigitRaw(1, B00001000);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(1, 0);
    matrix.writeDigitRaw(0, B00001000);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(0, B00010000);
    matrix.writeDisplay();
    delay(speed);
    matrix.writeDigitRaw(0, B00100000);
    matrix.writeDisplay();
    delay(speed);
  }
  matrix.writeDigitRaw(0, 1);
  matrix.writeDisplay();
  delay(speed);
  matrix.writeDigitRaw(0, 0);
  matrix.writeDigitRaw(1, 1);
  matrix.writeDisplay();
  delay(speed);
  matrix.writeDigitRaw(1, 0);
  matrix.writeDisplay();
}
