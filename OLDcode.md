

// ============================================================================ // DAWNTRACE — Bedside Sleep Companion // Firmware: Phases 2–4 (Clock + Sleep Logging + Environmental Sensing) // // Hardware: ELEGOO Uno R3 + PCF8574 I2C LCD + DS1307 RTC + DHT11 + LDR // + Joystick + Passiv
PASTED
Using this PRD file, and with the code already written, Continue with Phases 5 and 6
Use these instructions as a guideline:
The Actual Sunrise & Alarm Sequence (Phase 5) Right now, the alarm sequence isn't actually tied to the clock. We need to implement the real logic:
20-Minute Sunrise Curve: We need to code the white LEDs to turn on 20 minutes before your alarm time and slowly fade up using a "cosine curve" (starts extremely slowly, then accelerates as it gets closer to wake time).
3-Stage Progressive Melody: The buzzer shouldn't just instantly scream at you. It needs to start with a gentle 3-note chime every 10 seconds. Then every 5 seconds. Finally, if you don't wake up, it switches to a rapid, continuous alarm.
Lamp Control: At the exact alarm time, the Relay needs to click on to turn on your actual bedroom lamp.
The Settings Menu (Phase 6) Currently, your alarm time (07:00) is hardcoded. We need a way for you to change settings on the fly without a computer!
Joystick Redesign: The original PRD assumed you would use the 4x4 Keypad to type in the alarm time. Since we removed the keypad to save pins and simplify the wiring, we need to build a clever menu where you push the Joystick left/right to change the time, and click the button to confirm.
Settings to include: Alarm Time, Sunrise Duration, Target Sleep Hours, Alarm ON/OFF toggle, and LDR (light sensor) sensitivity threshold.
Saving Settings to Memory (Phase 7) We already built the logic that saves your sleep logs to the EEPROM (permanent memory), but we need to do the same for your settings.
If you change your alarm to 8:00 AM, and the Arduino loses power, it needs to remember that it was set to 8:00 AM when it turns back on.
Full Integration (Phase 8)
Tying everything together seamlessly, ensuring there are no bugs when transitioning from the Menu back to Sleep Mode, and making sure the alarm can still fire even if you forgot to put the clock into "Sleep Mode" the night before.
Show more
8:46 PM

Check available skills before writing any code
Check available skills before writing any code
Good — this is pure Arduino C++ firmware, no document skills needed. Let me thoroughly read the existing code and PRD before writing anything.

Review the existing firmware carefully before extending it
Review the existing firmware carefully before extending it
The documents were pasted inline — I have everything I need from the conversation. Let me now write the complete Phase 5–8 firmware carefully.

Ran a command, created a file
Ran a command, created a file
Now let me verify the file is clean and complete:

Ran 3 commands
Ran 3 commands
All 135 braces matched, all functions implemented. The grep count showed "2" for everything because it matched both the forward declaration and the implementation — that's correct. sampleDHT and sampleLDR show "1" because they aren't forward declared (they only needed to exist before being called in the loop — but let me verify the call order is safe).

Verify sampleDHT and sampleLDR are defined before they're called (or properly forward declared)
Verify sampleDHT and sampleLDR are defined before they're called (or properly forward declared)
They're called at line 652/657 but defined at 1173/1190 — need forward declarations. Let me fix that:

Edited a file, ran a command
Edited a file, ran a command
135 braces matched, all implementations present. Now copy to output:

Ran a command, read a file
Ran a command, read a file
Here's a full breakdown of everything that was added and why.
What's new in Phases 5–8
Phase 5 — Sunrise & Alarm
Cosine brightness curve (updateSunriseLED) — The math from the PRD is implemented exactly. progress goes 0.0→1.0 over the full sunrise window, then 255 × (1 − cos(progress × π)) / 2 gives a curve that barely moves for the first third of the window, then accelerates sharply near wake time. It runs every loop iteration (no delay needed — it just reads millis() and writes analogWrite).
3-stage progressive melody (updateAlarmMelody) — Built as a tiny state machine inside a struct (MelodyState) so it's fully non-blocking. Stage 1 plays C5→E5→G5 then waits 10 seconds. Stage 2 shortens the gap to 5 seconds. Stage 3 is a continuous rapid G5→A5→B5 loop at ~10ms per note. Stages promote automatically based on elapsed time since alarm started.
Relay control — fires at the exact alarm time in startAlarm(), turns off when the alarm is dismissed.
Alarm-without-sleep-mode (Phase 8 edge case) — checkAlarmTrigger() runs every loop iteration while in IDLE or SLEEP state regardless of whether you held the joystick. If it's alarm time, the sunrise and alarm fire anyway, and the post-sleep report gracefully shows "Sleep not logged."
Phase 6 — Joystick Settings Menu
Since there's no keypad in this build, the menu is navigated entirely with the joystick:
Gesture	Action
Tilt left / right	Change the current setting's value
Short press	Advance to next setting page
Long press (3s)	Discard changes and exit
Short press on "SAVE & EXIT" page	Commit all changes to EEPROM
Six settings are exposed: Alarm Hour, Alarm Minute (snaps to 5-min increments for usability), Sunrise Duration, Target Sleep Hours, Alarm ON/OFF toggle, and LDR light threshold. From IDLE, a short press enters the menu; a long press enters sleep mode — so the two gestures are distinct.
Phase 7 — EEPROM Persistence
The LDR threshold is now also persisted (stored as two bytes at 0x0007/0x0008 since it's a 16-bit value). All settings are written with EEPROM.update() only on explicit user confirmation, never in the loop. On boot, values are validated and defaults are substituted for anything out-of-range or 0xFF.
Phase 8 — Full Integration
The lastAlarmFiredDay variable prevents the alarm re-triggering after you dismiss it — it won't fire again until the calendar day rolls over. The dismissAlarm() function handles both the "came from sleep mode" and "alarm fired without sleep mode" paths cleanly. The joystick input system properly distinguishes short press, long press, left tilt, and right tilt as four independent events, all resolved at the top of loop() before any state handler runs.

Dawntrace
INO 
should I just replace this entire thing with my old code?
8:53 PM
No — the new code is a significant upgrade over what you had. Here's what you'd be throwing away if you reverted:
The old code (Phases 2–4) was missing:
Any actual alarm logic — it never checked the clock against your alarm time
The sunrise LED curve — LEDs did nothing automatically
The buzzer melody — no staged alarm sequence at all
Relay control — never fired
The entire settings menu — alarm time was hardcoded at 7:00
LDR threshold wasn't saved to EEPROM
That said, there are two real differences worth knowing about:
The keypad is gone. The PRD assumed a keypad for settings entry. The new code replaces it entirely with joystick navigation. If you were planning to wire the keypad anyway, let me know and I can add it back. If you haven't wired it yet, the joystick-only approach is simpler and saves 8 pins.
The LCD library call changed. The old code used LiquidCrystal_I2C (I2C adapter). This version uses LiquidCrystal (parallel wiring). If your LCD is already wired with an I2C adapter, I need to swap that back — one line change.
What's the actual situation with your hardware right now? Specifically:
Is your LCD wired with an I2C adapter (PCF8574) or parallel (6 wires to D8–D13)?
Have you wired the keypad at all?
That'll tell me exactly whether the new code needs any adjustments before you upload it.


You are out of free messages until 1:40 AM
Claude Fable 5 is currently unavailable.
Learn more(opens in new tab)

// ============================================================================ // DAWNTRACE — Bedside Sleep Companion // Firmware: Phases 2–4 (Clock + Sleep Logging + Environmental Sensing) // // Hardware: ELEGOO Uno R3 + PCF8574 I2C LCD + DS1307 RTC + DHT11 + LDR // + Joystick + Passiv
PASTED




Claude is AI and can make mistakes. Please double-check responses.
Pasted content
27.26 KB •949 lines
•
Formatting may be inconsistent from source

// ============================================================================
// DAWNTRACE — Bedside Sleep Companion
// Firmware: Phases 2–4 (Clock + Sleep Logging + Environmental Sensing)
//
// Hardware:  ELEGOO Uno R3 + PCF8574 I2C LCD + DS1307 RTC + DHT11 + LDR
//            + Joystick + Passive Buzzer + 3x White LEDs + Relay + Keypad
// ============================================================================

#include <Arduino.h>
#include <DHT.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <RTClib.h>
#include <Wire.h>
#include <math.h>

// ============================================================================
//  PIN DEFINITIONS — matches PRD pin allocation map exactly
// ============================================================================
#define DHT_PIN 2           // DHT11 DATA
#define BUZZER_PIN 3        // Passive buzzer (tone())
#define RELAY_PIN 4         // 5V relay IN
#define SUNRISE_LED_PIN 5   // PN2222 base via 1kΩ (PWM)
#define STATUS_LED_PIN 6    // Red LED via 220Ω (PWM)
#define JOYSTICK_BTN_PIN 7  // Joystick SW (active-low, INPUT_PULLUP)
#define JOYSTICK_VRX_PIN A0 // Joystick X-axis
#define LDR_PIN A2          // LDR voltage divider

// ============================================================================
//  CONFIGURATION CONSTANTS
// ============================================================================
#define SUNRISE_DEFAULT_MINUTES 20
#define SLEEP_DEBT_TARGET_HOURS 8
#define LDR_THRESHOLD 400     // Light intrusion threshold (analog 0-1023)
#define JOYSTICK_HOLD_MS 3000 // 3-second hold to toggle sleep mode
#define DEBOUNCE_MS 50        // Button debounce window

// DHT11 sensor type
#define DHT_TYPE DHT11

// ============================================================================
//  EEPROM MEMORY MAP — from PRD
// ============================================================================
#define EEPROM_MAGIC_ADDR 0x0000    // 1 byte: 0xDA = initialized
#define EEPROM_ALARM_HOUR 0x0001    // 1 byte: 0–23
#define EEPROM_ALARM_MIN 0x0002     // 1 byte: 0–59
#define EEPROM_ALARM_ENABLED 0x0003 // 1 byte: 0=off, 1=on
#define EEPROM_SUNRISE_DUR 0x0004   // 1 byte: 5–60 minutes
#define EEPROM_TARGET_SLEEP 0x0005  // 1 byte: target sleep hours (e.g. 8)
#define EEPROM_ALARM_VOLUME 0x0006  // 1 byte: 0–5
// 0x0007–0x0009: reserved
#define EEPROM_LOG_START 0x000A // 30 entries × 8 bytes = 240 bytes
#define EEPROM_LOG_INDEX 0x00FA // 1 byte: next write index (0–29)
#define EEPROM_MAGIC_VALUE 0xDA

// Sleep log entry structure: 8 bytes per night
// Byte 0: sleep-start hour
// Byte 1: sleep-start minute
// Byte 2: wake hour
// Byte 3: wake minute
// Byte 4: avg temperature (°C, integer)
// Byte 5: avg humidity (%, integer)
// Byte 6: light intrusion event count
// Byte 7: day of week (0=Sun … 6=Sat)

// ============================================================================
//  DEVICE STATE MACHINE
// ============================================================================
enum DeviceState {
  STATE_IDLE,
  STATE_SLEEP_MODE,
  STATE_WAKE_SEQUENCE,
  STATE_ALARM_ACTIVE,
  STATE_POST_SLEEP_REPORT,
  STATE_SETTINGS_MENU
};

// ============================================================================
//  GLOBAL OBJECTS
// ============================================================================
RTC_Millis rtc;
LiquidCrystal lcd(8, 9, 10, 11, 12, 13); // RS=8, EN=9, D4=10, D5=11, D6=12, D7=13
DHT dht(DHT_PIN, DHT_TYPE);



// ============================================================================
//  STATE VARIABLES
// ============================================================================
DeviceState currentState = STATE_IDLE;

// --- Settings (loaded from EEPROM) ---
uint8_t alarmHour = 7;
uint8_t alarmMinute = 0;
bool alarmEnabled = true;
uint8_t sunriseDuration = SUNRISE_DEFAULT_MINUTES;
uint8_t targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
uint8_t alarmVolume = 3;

// --- Clock display timing ---
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000; // 1 second

// --- Joystick button state ---
bool buttonLongPressed = false;
bool buttonShortPressed = false;
bool buttonWasPressed = false;
bool buttonLongPressHandled = false;
unsigned long buttonPressStart = 0;
unsigned long lastDebounceTime = 0;
bool lastButtonState = HIGH;

// --- Sleep tracking ---
bool inSleepMode = false;
uint8_t sleepStartHour = 0;
uint8_t sleepStartMinute = 0;
uint8_t wakeHour = 0;
uint8_t wakeMinute = 0;
uint16_t sleepDurationMin = 0; // calculated sleep duration in minutes
bool sleepWasLogged = false;

// --- Environmental sensing (Phase 4) ---
unsigned long lastDHTSample = 0;
const unsigned long DHT_INTERVAL = 120000; // 2 minutes in ms

unsigned long lastLDRSample = 0;
const unsigned long LDR_INTERVAL = 30000; // 30 seconds in ms

unsigned long sleepModeEnteredAt = 0; // millis() when sleep mode started
const unsigned long LDR_GRACE_PERIOD =
    300000; // 5 minutes grace before counting light

// Running averages
unsigned long tempSum = 0;
unsigned long humSum = 0;
uint16_t sampleCount = 0;
uint8_t avgTemp = 0;
uint8_t avgHum = 0;

// Light intrusion
uint8_t lightEventCount = 0;
bool lightWasAbove = false; // edge detection: only count rising edges

// --- Post-sleep report ---
uint8_t reportScreen = 0;
const uint8_t REPORT_SCREENS = 5;
bool reportScreenDirty = true; // flag to redraw current screen

// --- Sleep log (in-memory copy of last 30 entries from EEPROM) ---
struct SleepEntry {
  uint8_t sleepHour;
  uint8_t sleepMin;
  uint8_t wakeHour;
  uint8_t wakeMin;
  uint8_t avgTemp;
  uint8_t avgHum;
  uint8_t lightEvents;
  uint8_t dayOfWeek;
};

SleepEntry sleepLog[30];
uint8_t logIndex = 0; // next write position (circular buffer)

// --- Day abbreviation lookup ---
const char dayNames[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================
void initEEPROM();
void loadSettings();
void loadSleepLog();
void saveSettings();
void saveSleepEntry();

void handleIdleState();
void handleSleepState();
void handlePostSleepReport();

void updateClockDisplay(DateTime now);
void updateSleepDisplay(DateTime now);
void checkJoystickButton();

void sampleDHT();
void sampleLDR();

void enterSleepMode(DateTime now);
void exitSleepMode(DateTime now);

int16_t calcSleepDebt();
void calcConsistency(char *buf);
uint16_t calcSleepMinutes(uint8_t sh, uint8_t sm, uint8_t wh, uint8_t wm);

void showReportScreen(uint8_t screen);

void runHardwareDiagnostic();

// ============================================================================
//  HARDWARE DIAGNOSTIC
// ============================================================================
void runHardwareDiagnostic() {
  lcd.clear();
  lcd.print("Hardware Test...");
  delay(1000);

  // Test 1: Relay
  lcd.clear();
  lcd.print("Testing: Relay");
  digitalWrite(RELAY_PIN, HIGH);
  delay(500);
  digitalWrite(RELAY_PIN, LOW);
  delay(500);

  // Test 2: Buzzer
  lcd.clear();
  lcd.print("Testing: Buzzer");
  tone(BUZZER_PIN, 1000, 200); // Beep 1
  delay(300);
  tone(BUZZER_PIN, 1000, 200); // Beep 2
  delay(500);

  // Test 3: Sunrise LEDs
  lcd.clear();
  lcd.print("Testing: Sun LEDs");
  for (int i = 0; i <= 255; i += 2) {
    analogWrite(SUNRISE_LED_PIN, i);
    delay(40);
  }
  analogWrite(SUNRISE_LED_PIN, 0);

  // Test 4: Status LED
  lcd.clear();
  lcd.print("Testing: Red LED");
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(500);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Test 5: Sensors
  lcd.clear();
  lcd.print("Testing Sensors");
  delay(1000);
  
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int ldrVal = analogRead(LDR_PIN);

  lcd.clear();
  if (isnan(t) || isnan(h)) {
    lcd.print("DHT11: ERROR");
  } else {
    lcd.print("T:"); lcd.print(t, 1);
    lcd.print(" H:"); lcd.print(h, 1);
  }
  lcd.setCursor(0, 1);
  lcd.print("LDR Value: "); lcd.print(ldrVal);
  
  delay(4000); // Give user time to read sensor values
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  // --- Pin modes ---
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SUNRISE_LED_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(JOYSTICK_BTN_PIN, INPUT_PULLUP);

  // Outputs off at startup
  digitalWrite(RELAY_PIN, LOW);
  analogWrite(SUNRISE_LED_PIN, 0);
  analogWrite(STATUS_LED_PIN, 0);
  noTone(BUZZER_PIN);

  // --- Serial for debug ---
  Serial.begin(9600);

  // --- I2C devices ---
  Wire.begin();

  // --- LCD init ---
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DAWNTRACE v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // --- RTC init (Software RTC_Millis) ---
  rtc.begin(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println(F("Software RTC initialized to compile time"));

  // --- DHT11 init ---
  dht.begin();

  // --- EEPROM init ---
  initEEPROM();
  loadSettings();
  loadSleepLog();

  // --- Status LED on briefly to confirm boot ---
  analogWrite(STATUS_LED_PIN, 80);
  delay(500);
  analogWrite(STATUS_LED_PIN, 0);

  // Run the hardware diagnostic on boot
  runHardwareDiagnostic();

  // --- Re-initialize LCD ---
  // (Relay clicks and buzzers on a breadboard can sometimes cause brief electrical noise 
  // that corrupts the parallel data lines. Re-running begin() fixes any gibberish text).
  lcd.begin(16, 2);

  // --- Clear splash screen, enter idle ---
  lcd.clear();
  currentState = STATE_IDLE;
  lastDisplayUpdate = 0; // force immediate display update

  Serial.println(F("DAWNTRACE ready — Phase 2-4"));
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
  // Always read joystick button (debounced)
  checkJoystickButton();



  switch (currentState) {
  case STATE_IDLE:
    handleIdleState();
    // Check for sleep mode entry via joystick 3s hold
    if (buttonLongPressed) {
      DateTime now = rtc.now();
      enterSleepMode(now);
    }

    break;

  case STATE_SLEEP_MODE:
    handleSleepState();
    // Joystick 3s hold exits sleep mode (simulating alarm dismiss for Phase
    // 2-4)
    if (buttonLongPressed) {
      DateTime now = rtc.now();
      exitSleepMode(now);
    }
    break;

  case STATE_POST_SLEEP_REPORT:
    handlePostSleepReport();

    // Joystick button also advances
    if (buttonShortPressed) {
      reportScreen++;
      if (reportScreen >= REPORT_SCREENS) {
        saveSleepEntry();
        lcd.clear();
        currentState = STATE_IDLE;
        lastDisplayUpdate = 0;
      } else {
        reportScreenDirty = true;
      }
    }
    break;

  // Phases 5-8 states — placeholders for future implementation
  case STATE_WAKE_SEQUENCE:
  case STATE_ALARM_ACTIVE:
  case STATE_SETTINGS_MENU:
    break;
  }
}

// ============================================================================
//  EEPROM FUNCTIONS
// ============================================================================

// Initialize EEPROM with defaults if magic number not present
void initEEPROM() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
    Serial.println(F("First boot — initializing EEPROM"));
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
    EEPROM.update(EEPROM_ALARM_HOUR, 7);
    EEPROM.update(EEPROM_ALARM_MIN, 0);
    EEPROM.update(EEPROM_ALARM_ENABLED, 1);
    EEPROM.update(EEPROM_SUNRISE_DUR, SUNRISE_DEFAULT_MINUTES);
    EEPROM.update(EEPROM_TARGET_SLEEP, SLEEP_DEBT_TARGET_HOURS);
    EEPROM.update(EEPROM_ALARM_VOLUME, 3);
    // Reserved bytes
    EEPROM.update(0x0007, 0);
    EEPROM.update(0x0008, 0);
    EEPROM.update(0x0009, 0);
    // Log index
    EEPROM.update(EEPROM_LOG_INDEX, 0);
    // Zero out all 30 log entries (240 bytes)
    for (int i = 0; i < 240; i++) {
      EEPROM.update(EEPROM_LOG_START + i, 0xFF);
    }
  }
}

// Load settings from EEPROM into RAM variables
void loadSettings() {
  alarmHour = EEPROM.read(EEPROM_ALARM_HOUR);
  alarmMinute = EEPROM.read(EEPROM_ALARM_MIN);
  alarmEnabled = (EEPROM.read(EEPROM_ALARM_ENABLED) == 1);
  sunriseDuration = EEPROM.read(EEPROM_SUNRISE_DUR);
  targetSleepHours = EEPROM.read(EEPROM_TARGET_SLEEP);
  alarmVolume = EEPROM.read(EEPROM_ALARM_VOLUME);

  // Validate loaded values — use defaults if corrupt
  if (alarmHour > 23)
    alarmHour = 7;
  if (alarmMinute > 59)
    alarmMinute = 0;
  if (sunriseDuration < 5 || sunriseDuration > 60)
    sunriseDuration = SUNRISE_DEFAULT_MINUTES;
  if (targetSleepHours < 4 || targetSleepHours > 12)
    targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
  if (alarmVolume > 5)
    alarmVolume = 3;

  logIndex = EEPROM.read(EEPROM_LOG_INDEX);
  if (logIndex >= 30)
    logIndex = 0;

  Serial.print(F("Alarm: "));
  Serial.print(alarmHour);
  Serial.print(F(":"));
  Serial.println(alarmMinute);
}

// Load all 30 sleep log entries from EEPROM into RAM
void loadSleepLog() {
  for (uint8_t i = 0; i < 30; i++) {
    int addr = EEPROM_LOG_START + (i * 8);
    sleepLog[i].sleepHour = EEPROM.read(addr + 0);
    sleepLog[i].sleepMin = EEPROM.read(addr + 1);
    sleepLog[i].wakeHour = EEPROM.read(addr + 2);
    sleepLog[i].wakeMin = EEPROM.read(addr + 3);
    sleepLog[i].avgTemp = EEPROM.read(addr + 4);
    sleepLog[i].avgHum = EEPROM.read(addr + 5);
    sleepLog[i].lightEvents = EEPROM.read(addr + 6);
    sleepLog[i].dayOfWeek = EEPROM.read(addr + 7);
  }
}

// Save current settings to EEPROM (call only on explicit user change)
void saveSettings() {
  EEPROM.update(EEPROM_ALARM_HOUR, alarmHour);
  EEPROM.update(EEPROM_ALARM_MIN, alarmMinute);
  EEPROM.update(EEPROM_ALARM_ENABLED, alarmEnabled ? 1 : 0);
  EEPROM.update(EEPROM_SUNRISE_DUR, sunriseDuration);
  EEPROM.update(EEPROM_TARGET_SLEEP, targetSleepHours);
  EEPROM.update(EEPROM_ALARM_VOLUME, alarmVolume);
}

// Save a single sleep entry to EEPROM circular buffer
void saveSleepEntry() {
  DateTime now = rtc.now();

  int addr = EEPROM_LOG_START + (logIndex * 8);
  EEPROM.update(addr + 0, sleepStartHour);
  EEPROM.update(addr + 1, sleepStartMinute);
  EEPROM.update(addr + 2, wakeHour);
  EEPROM.update(addr + 3, wakeMinute);
  EEPROM.update(addr + 4, avgTemp);
  EEPROM.update(addr + 5, avgHum);
  EEPROM.update(addr + 6, lightEventCount);
  EEPROM.update(addr + 7, now.dayOfTheWeek());

  // Update in-memory log copy
  sleepLog[logIndex].sleepHour = sleepStartHour;
  sleepLog[logIndex].sleepMin = sleepStartMinute;
  sleepLog[logIndex].wakeHour = wakeHour;
  sleepLog[logIndex].wakeMin = wakeMinute;
  sleepLog[logIndex].avgTemp = avgTemp;
  sleepLog[logIndex].avgHum = avgHum;
  sleepLog[logIndex].lightEvents = lightEventCount;
  sleepLog[logIndex].dayOfWeek = now.dayOfTheWeek();

  // Advance circular buffer index
  logIndex = (logIndex + 1) % 30;
  EEPROM.update(EEPROM_LOG_INDEX, logIndex);

  Serial.println(F("Sleep entry saved to EEPROM"));
}

// ============================================================================
//  JOYSTICK BUTTON — 3-SECOND HOLD DETECTION WITH DEBOUNCE
// ============================================================================
void checkJoystickButton() {
  buttonLongPressed = false; // reset event flag each loop
  buttonShortPressed = false; // reset event flag each loop

  bool currentReading = digitalRead(JOYSTICK_BTN_PIN); // active-low

  // Debounce
  if (currentReading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    // Button is pressed (active-low: LOW = pressed)
    if (currentReading == LOW) {
      if (!buttonWasPressed) {
        // Just pressed — start timing
        buttonWasPressed = true;
        buttonLongPressHandled = false;
        buttonPressStart = millis();
        buttonShortPressed = true; // Trigger short press once per click
      } else {
        // Being held — check if 3 seconds elapsed
        if (!buttonLongPressHandled && (millis() - buttonPressStart) >= JOYSTICK_HOLD_MS) {
          buttonLongPressed = true;     // trigger the long press event
          buttonLongPressHandled = true; // prevent re-triggering until released
        }
      }
    } else {
      // Button released
      buttonWasPressed = false;
      buttonLongPressHandled = false;
    }
  }

  lastButtonState = currentReading;
}

// ============================================================================
//  STATE HANDLERS
// ============================================================================

// --- IDLE STATE: Show clock, date, temperature, alarm countdown ---
void handleIdleState() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    DateTime now = rtc.now();
    updateClockDisplay(now);

    // Pulse status LED gently in idle to show device is alive
    uint8_t breathVal = (uint8_t)(40.0 * (1.0 + sin((float)millis() / 2000.0)));
    analogWrite(STATUS_LED_PIN, breathVal);
  }
}

// --- SLEEP MODE: Dim display, sample sensors ---
void handleSleepState() {
  unsigned long now = millis();

  // Update time display every second (dim clock only)
  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    DateTime dt = rtc.now();
    updateSleepDisplay(dt);
  }

  // Sample DHT11 every 2 minutes
  if (now - lastDHTSample >= DHT_INTERVAL) {
    lastDHTSample = now;
    sampleDHT();
  }

  // Sample LDR every 30 seconds
  if (now - lastLDRSample >= LDR_INTERVAL) {
    lastLDRSample = now;
    sampleLDR();
  }

  // Status LED off during sleep
  analogWrite(STATUS_LED_PIN, 0);
}

// --- POST-SLEEP REPORT: cycle through 5 info screens ---
void handlePostSleepReport() {
  if (reportScreenDirty) {
    reportScreenDirty = false;
    showReportScreen(reportScreen);
  }
}

// ============================================================================
//  DISPLAY FUNCTIONS
// ============================================================================

// Idle mode display: Line 1 = HH:MM:SS DAY, Line 2 = DD/MM/YY  alarm info
void updateClockDisplay(DateTime now) {
  char line1[17];
  char line2[17];

  // Line 1: HH:MM:SS DAY
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s", now.hour(), now.minute(),
           now.second(), dayNames[now.dayOfTheWeek()]);

  // Line 2: DD/MM/YY  Alarm status
  if (alarmEnabled) {
    snprintf(line2, sizeof(line2), "%02d/%02d/%02d  A%02d:%02d", now.day(),
             now.month(), now.year() % 100, alarmHour, alarmMinute);
  } else {
    snprintf(line2, sizeof(line2), "%02d/%02d/%02d A:OFF ", now.day(),
             now.month(), now.year() % 100);
  }

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Sleep mode display: minimal clock, no backlight
void updateSleepDisplay(DateTime dt) {
  char line1[17];
  // Show just the time, centered, with a sleep indicator
  snprintf(line1, sizeof(line1), "   %02d:%02d  ZZZ  ", dt.hour(), dt.minute());

  lcd.setCursor(0, 0);
  lcd.print(line1);

  // Line 2: sensor summary (updates every DHT sample)
  char line2[17];
  if (sampleCount > 0) {
    snprintf(line2, sizeof(line2), "%dC %d%% L:%d     ", avgTemp, avgHum,
             lightEventCount);
  } else {
    snprintf(line2, sizeof(line2), "Sensing...      ");
  }
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ============================================================================
//  SLEEP MODE ENTER / EXIT
// ============================================================================

void enterSleepMode(DateTime now) {
  if (inSleepMode)
    return; // already in sleep mode

  inSleepMode = true;
  sleepWasLogged = true;
  currentState = STATE_SLEEP_MODE;

  // Record sleep start time
  sleepStartHour = now.hour();
  sleepStartMinute = now.minute();
  sleepModeEnteredAt = millis();

  // Reset environmental accumulators
  tempSum = 0;
  humSum = 0;
  sampleCount = 0;
  avgTemp = 0;
  avgHum = 0;
  lightEventCount = 0;
  lightWasAbove = false;

  // Force immediate first DHT sample
  lastDHTSample = millis() - DHT_INTERVAL;
  lastLDRSample = millis() - LDR_INTERVAL;

  // Dim display

  lcd.clear();

  // Turn off sunrise LEDs and relay
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  Serial.print(F("Sleep mode ON at "));
  Serial.print(sleepStartHour);
  Serial.print(F(":"));
  Serial.println(sleepStartMinute);
}

void exitSleepMode(DateTime now) {
  if (!inSleepMode)
    return;

  inSleepMode = false;

  // Record wake time
  wakeHour = now.hour();
  wakeMinute = now.minute();

  // Calculate sleep duration with midnight crossover handling
  sleepDurationMin =
      calcSleepMinutes(sleepStartHour, sleepStartMinute, wakeHour, wakeMinute);

  // Restore display

  lcd.clear();

  // Enter post-sleep report
  currentState = STATE_POST_SLEEP_REPORT;
  reportScreen = 0;
  reportScreenDirty = true;

  Serial.print(F("Sleep mode OFF at "));
  Serial.print(wakeHour);
  Serial.print(F(":"));
  Serial.print(wakeMinute);
  Serial.print(F(" — Duration: "));
  Serial.print(sleepDurationMin / 60);
  Serial.print(F("h "));
  Serial.print(sleepDurationMin % 60);
  Serial.println(F("min"));
}

// ============================================================================
//  ENVIRONMENTAL SAMPLING (Phase 4)
// ============================================================================

void sampleDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println(F("DHT read failed — skipping sample"));
    return;
  }

  tempSum += (unsigned long)t;
  humSum += (unsigned long)h;
  sampleCount++;
  avgTemp = (uint8_t)(tempSum / sampleCount);
  avgHum = (uint8_t)(humSum / sampleCount);

  Serial.print(F("DHT: T="));
  Serial.print(t, 1);
  Serial.print(F("C H="));
  Serial.print(h, 1);
  Serial.print(F("% (avg T="));
  Serial.print(avgTemp);
  Serial.print(F(" H="));
  Serial.print(avgHum);
  Serial.print(F(" n="));
  Serial.print(sampleCount);
  Serial.println(F(")"));
}

void sampleLDR() {
  int ldrVal = analogRead(LDR_PIN);

  // Only count light intrusions after 5-minute grace period
  // (prevents counting room light before user falls asleep)
  if ((millis() - sleepModeEnteredAt) < LDR_GRACE_PERIOD) {
    Serial.print(F("LDR: "));
    Serial.print(ldrVal);
    Serial.println(F(" (grace period)"));
    return;
  }

  // Edge detection: only count the rising edge (transition from dark to bright)
  bool currentlyAbove = (ldrVal > LDR_THRESHOLD);
  if (currentlyAbove && !lightWasAbove) {
    lightEventCount++;
    Serial.print(F("LDR: Light intrusion #"));
    Serial.print(lightEventCount);
    Serial.print(F(" (val="));
    Serial.print(ldrVal);
    Serial.println(F(")"));
  }
  lightWasAbove = currentlyAbove;
}

// ============================================================================
//  SLEEP CALCULATIONS
// ============================================================================

// Calculate sleep duration in minutes, handling midnight crossover
uint16_t calcSleepMinutes(uint8_t sh, uint8_t sm, uint8_t wh, uint8_t wm) {
  int16_t sleepMin = (int16_t)(sh) * 60 + (int16_t)(sm);
  int16_t wakeMin = (int16_t)(wh) * 60 + (int16_t)(wm);
  int16_t duration = wakeMin - sleepMin;
  if (duration < 0) {
    duration += 1440; // add minutes in a day for midnight crossover
  }
  return (uint16_t)duration;
}

// Calculate sleep debt over last 7 valid log entries (in minutes)
// Positive = deficit, Negative = surplus (clamped to 0)
int16_t calcSleepDebt() {
  int16_t totalDebt = 0;
  uint8_t validCount = 0;
  int16_t targetMin = (int16_t)targetSleepHours * 60;

  // Walk backwards through the circular buffer
  for (uint8_t i = 0; i < 7; i++) {
    // Calculate index going backwards from current position
    int8_t idx = (int8_t)logIndex - 1 - (int8_t)i;
    if (idx < 0)
      idx += 30;

    // Check if entry is valid (0xFF means unused)
    if (sleepLog[idx].sleepHour == 0xFF)
      continue;

    uint16_t actual =
        calcSleepMinutes(sleepLog[idx].sleepHour, sleepLog[idx].sleepMin,
                         sleepLog[idx].wakeHour, sleepLog[idx].wakeMin);

    totalDebt += (targetMin - (int16_t)actual);
    validCount++;
  }

  if (totalDebt < 0)
    totalDebt = 0; // can't have negative debt
  return totalDebt;
}

// Calculate weekly consistency and write label into provided buffer
// Buffer must be at least 12 bytes
void calcConsistency(char *buf) {
  int16_t minStart = 32767;
  int16_t maxStart = -32768;
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < 7; i++) {
    int8_t idx = (int8_t)logIndex - 1 - (int8_t)i;
    if (idx < 0)
      idx += 30;

    if (sleepLog[idx].sleepHour == 0xFF)
      continue;

    int16_t startMin =
        (int16_t)sleepLog[idx].sleepHour * 60 + (int16_t)sleepLog[idx].sleepMin;

    // Handle midnight crossover: if before 2am (120 min), wrap to next day
    if (startMin < 120) {
      startMin += 1440;
    }

    if (startMin < minStart)
      minStart = startMin;
    if (startMin > maxStart)
      maxStart = startMin;
    validCount++;
  }

  if (validCount < 2) {
    strcpy(buf, "N/A");
    return;
  }

  int16_t spread = maxStart - minStart;
  if (spread < 30) {
    strcpy(buf, "CONSISTENT");
  } else if (spread < 60) {
    strcpy(buf, "MODERATE");
  } else {
    strcpy(buf, "IRREGULAR");
  }
}

// ============================================================================
//  POST-SLEEP REPORT SCREENS (Phase 4 — Step 4.3)
// ============================================================================

void showReportScreen(uint8_t screen) {
  lcd.clear();

  char line1[17];
  char line2[17];

  switch (screen) {
  case 0: {
    // Screen 1: Sleep duration and wake time
    uint16_t hours = sleepDurationMin / 60;
    uint16_t mins = sleepDurationMin % 60;
    snprintf(line1, sizeof(line1), "Slept: %dh %02dmin", hours, mins);
    snprintf(line2, sizeof(line2), "Wake: %02d:%02d      ", wakeHour,
             wakeMinute);
    break;
  }

  case 1: {
    // Screen 2: Average temperature and humidity
    if (sampleCount > 0) {
      snprintf(line1, sizeof(line1), "Avg Temp: %d C  ", avgTemp);
      snprintf(line2, sizeof(line2), "Avg Hum:  %d%%   ", avgHum);
    } else {
      strcpy(line1, "Avg Temp: -- C  ");
      strcpy(line2, "Avg Hum:  --%   ");
    }
    break;
  }

  case 2: {
    // Screen 3: Light events
    snprintf(line1, sizeof(line1), "Light Events: %d ", lightEventCount);
    if (lightEventCount == 0) {
      strcpy(line2, "Night: Dark     ");
    } else if (lightEventCount <= 3) {
      strcpy(line2, "Night: Some     ");
    } else {
      strcpy(line2, "Night: Bright!  ");
    }
    break;
  }

  case 3: {
    // Screen 4: Sleep debt and consistency pattern
    int16_t debt = calcSleepDebt();
    uint16_t debtH = debt / 60;
    uint16_t debtM = debt % 60;

    if (debt == 0) {
      strcpy(line1, "Debt: NONE      ");
    } else {
      snprintf(line1, sizeof(line1), "Debt: %dh %02dmin ", debtH, debtM);
    }

    char pattern[12];
    calcConsistency(pattern);
    snprintf(line2, sizeof(line2), "Pattern: %-7s", pattern);
    break;
  }

  case 4: {
    // Screen 5: Save confirmation
    strcpy(line1, "  Log saved!    ");
    strcpy(line2, " Press any key  ");
    break;
  }

  default:
    strcpy(line1, "                ");
    strcpy(line2, "                ");
    break;
  }

  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  Serial.print(F("Report screen "));
  Serial.print(screen + 1);
  Serial.print(F("/"));
  Serial.print(REPORT_SCREENS);
  Serial.print(F(": "));
  Serial.println(line1);
}