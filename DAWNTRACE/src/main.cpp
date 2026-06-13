// ============================================================================
// DAWNTRACE — Bedside Sleep Companion
// Firmware: Phases 2–8 Complete
//   Phase 2:  Clock + Display (RTC_Millis, LCD parallel)
//   Phase 3:  Sleep Logging, debt calc, consistency score
//   Phase 4:  Environmental Sensing (DHT11 + LDR)
//   Phase 5:  Sunrise curve + 3-stage alarm melody + relay
//   Phase 6:  Joystick-driven Settings Menu (NO keypad)
//   Phase 7:  Full EEPROM persistence for settings
//   Phase 8:  Full state-machine integration & edge cases
//
// Hardware:   ELEGOO Uno R3
//             LCD1602 wired in PARALLEL (NO I2C adapter needed here —
//             parallel uses pins D8–D13; see note at LCD init below)
//             DS1307 RTC on I2C (A4/A5)
//             DHT11 on D2
//             Passive Buzzer on D3
//             5V Relay on D4
//             Sunrise LEDs via PN2222 on D5 (PWM)
//             Red Status LED on D6
//             Joystick: SW=D7, VRx=A0
//             LDR voltage divider on A2
//
// IMPORTANT PIN CHANGE vs original PRD:
//   The original PRD assumed a keypad on D8–D13 + A1 + A3, plus an I2C LCD.
//   This build uses a PARALLEL LCD on D8–D13 instead of the keypad, freeing
//   us from needing the I2C adapter AND the keypad simultaneously.
//   All settings are navigated via the joystick (VRx for left/right,
//   SW for confirm/enter). This is the revised design per user instructions.
//
// ============================================================================

#include <Arduino.h>
#include <DHT.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <RTClib.h>
#include <Wire.h>
#include <math.h>

// ============================================================================
//  PIN DEFINITIONS
// ============================================================================
#define DHT_PIN          2   // DHT11 DATA
#define BUZZER_PIN       3   // Passive buzzer — tone() uses Timer2
#define RELAY_PIN        4   // 5V relay IN
#define SUNRISE_LED_PIN  5   // PN2222 base via 1kΩ (PWM, Timer0)
#define STATUS_LED_PIN   6   // Red LED via 220Ω (PWM, Timer0)
#define JOYSTICK_BTN_PIN 7   // Joystick SW (active-low, INPUT_PULLUP)
// LCD parallel: RS=8, EN=9, D4=10, D5=11, D6=12, D7=13
#define JOYSTICK_VRX_PIN A0  // Joystick X-axis (0=full-left, 1023=full-right)
#define LDR_PIN          A2  // LDR voltage divider

// ============================================================================
//  CONFIGURATION CONSTANTS
// ============================================================================
#define SUNRISE_DEFAULT_MINUTES  20
#define SLEEP_DEBT_TARGET_HOURS   8
#define LDR_THRESHOLD           400   // Light intrusion threshold (0–1023)
#define JOYSTICK_HOLD_MS       3000   // ms hold to toggle sleep/settings
#define JOYSTICK_SHORT_MS       150   // ms for a confirmed short press
#define DEBOUNCE_MS              50   // Button debounce window

// Joystick X-axis thresholds (calibrate if needed)
#define JOY_LEFT_THRESHOLD      300   // < this = left tilt
#define JOY_RIGHT_THRESHOLD     700   // > this = right tilt
#define JOY_CENTER_HYSTERESIS    50   // dead-band around center (512 ± this)

// Alarm melody frequencies (Hz)
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988

#define DHT_TYPE DHT11

// ============================================================================
//  EEPROM MEMORY MAP
// ============================================================================
#define EEPROM_MAGIC_ADDR    0x0000  // 1 byte: 0xDA = initialized
#define EEPROM_ALARM_HOUR    0x0001  // 1 byte: 0–23
#define EEPROM_ALARM_MIN     0x0002  // 1 byte: 0–59
#define EEPROM_ALARM_ENABLED 0x0003  // 1 byte: 0=off 1=on
#define EEPROM_SUNRISE_DUR   0x0004  // 1 byte: 5–60 minutes
#define EEPROM_TARGET_SLEEP  0x0005  // 1 byte: 4–12 hours
#define EEPROM_ALARM_VOLUME  0x0006  // 1 byte: 0–5
#define EEPROM_LDR_THRESH_H  0x0007  // 2 bytes: LDR threshold (high byte)
#define EEPROM_LDR_THRESH_L  0x0008  // 2 bytes: LDR threshold (low byte)
// 0x0009: reserved
#define EEPROM_LOG_START     0x000A  // 30 entries × 8 bytes = 240 bytes
#define EEPROM_LOG_INDEX     0x00FA  // 1 byte: next write index (0–29)
#define EEPROM_MAGIC_VALUE   0xDA

// ============================================================================
//  DEVICE STATE MACHINE
// ============================================================================
enum DeviceState {
  STATE_IDLE,
  STATE_SLEEP_MODE,
  STATE_WAKE_SEQUENCE,   // LED ramp active, gentle melody
  STATE_ALARM_ACTIVE,    // Full alarm until dismissed
  STATE_POST_SLEEP_REPORT,
  STATE_SETTINGS_MENU
};

// ============================================================================
//  SETTINGS MENU SUB-STATES
// ============================================================================
enum MenuPage {
  MENU_ALARM_HOUR,       // 0
  MENU_ALARM_MIN,        // 1
  MENU_SUNRISE_DUR,      // 2
  MENU_TARGET_SLEEP,     // 3
  MENU_ALARM_TOGGLE,     // 4
  MENU_LDR_THRESH,       // 5
  MENU_SAVE_EXIT,        // 6
  MENU_PAGE_COUNT        // sentinel
};

// ============================================================================
//  GLOBAL OBJECTS
// ============================================================================
// Parallel LCD: RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(8, 9, 10, 11, 12, 13);
RTC_Millis    rtc;
DHT           dht(DHT_PIN, DHT_TYPE);

// ============================================================================
//  SETTINGS (loaded from / saved to EEPROM)
// ============================================================================
uint8_t  alarmHour      = 7;
uint8_t  alarmMinute    = 0;
bool     alarmEnabled   = true;
uint8_t  sunriseDuration = SUNRISE_DEFAULT_MINUTES;  // minutes
uint8_t  targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
uint8_t  alarmVolume    = 3;
uint16_t ldrThreshold   = LDR_THRESHOLD;

// Temporary copies edited in menu (committed on MENU_SAVE_EXIT)
uint8_t  menuAlarmHour;
uint8_t  menuAlarmMin;
uint8_t  menuSunriseDur;
uint8_t  menuTargetSleep;
bool     menuAlarmEnabled;
uint16_t menuLdrThresh;

// ============================================================================
//  STATE VARIABLES
// ============================================================================
DeviceState currentState = STATE_IDLE;
MenuPage    currentMenuPage = MENU_ALARM_HOUR;

// Clock display timing
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000;

// ============================================================================
//  JOYSTICK INPUT
// ============================================================================
// Button
bool     btnWasPressed          = false;
bool     btnLongPressHandled    = false;
unsigned long btnPressStart     = 0;
unsigned long lastDebounceTime  = 0;
bool     lastBtnReading         = HIGH;

// Events (reset each loop)
bool evtShortPress   = false;
bool evtLongPress    = false;

// Joystick axis — fires one event per tilt, requires return to centre
bool joyWasLeft      = false;
bool joyWasRight     = false;
bool evtJoyLeft      = false;
bool evtJoyRight     = false;

// ============================================================================
//  SLEEP TRACKING
// ============================================================================
bool     inSleepMode        = false;
uint8_t  sleepStartHour     = 0;
uint8_t  sleepStartMinute   = 0;
uint8_t  wakeHour           = 0;
uint8_t  wakeMinute         = 0;
uint16_t sleepDurationMin   = 0;
bool     sleepWasLogged     = false;

// ============================================================================
//  ENVIRONMENTAL SENSING
// ============================================================================
unsigned long lastDHTSample     = 0;
unsigned long lastLDRSample     = 0;
const unsigned long DHT_INTERVAL = 120000UL;  // 2 min
const unsigned long LDR_INTERVAL =  30000UL;  // 30 sec

unsigned long sleepModeEnteredAt = 0;
const unsigned long LDR_GRACE_PERIOD = 300000UL;  // 5 min

unsigned long tempSum   = 0;
unsigned long humSum    = 0;
uint16_t      sampleCount = 0;
uint8_t       avgTemp   = 0;
uint8_t       avgHum    = 0;

uint8_t  lightEventCount = 0;
bool     lightWasAbove   = false;

// ============================================================================
//  SUNRISE / ALARM (Phase 5)
// ============================================================================
// The sunrise LED ramp begins (sunriseDuration) minutes BEFORE alarmHour:alarmMinute.
// At the exact alarm time, relay fires + Stage-1 melody starts.

bool     sunriseActive         = false;
unsigned long sunriseStartMs   = 0;
unsigned long sunriseTotalMs   = 0;

bool     alarmFiring           = false;
unsigned long alarmStartMs     = 0;

// Alarm melody state
uint8_t  melodyNoteIndex       = 0;   // which note in sequence
unsigned long lastNoteTime     = 0;   // millis of last note play

// Stage thresholds
const unsigned long STAGE2_MS = 120000UL;  // 2 min
const unsigned long STAGE3_MS = 300000UL;  // 5 min

// Track whether we already triggered sunrise/alarm for this alarm time today
uint8_t  lastAlarmFiredDay    = 255;  // day-of-month when alarm last fired

// ============================================================================
//  POST-SLEEP REPORT
// ============================================================================
uint8_t  reportScreen    = 0;
const uint8_t REPORT_SCREENS = 5;
bool     reportScreenDirty = true;

// ============================================================================
//  SLEEP LOG
// ============================================================================
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
uint8_t    logIndex = 0;

const char dayNames[7][4] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================

// EEPROM
void initEEPROM();
void loadSettings();
void loadSleepLog();
void saveSettings();
void saveSleepEntry();

// Input
void checkJoystick();

// State handlers
void handleIdleState();
void handleSleepState();
void handleWakeSequence();
void handleAlarmActive();
void handlePostSleepReport();
void handleSettingsMenu();

// State transitions
void enterSleepMode(DateTime now);
void exitSleepMode(DateTime now);
void checkAlarmTrigger(DateTime now);
void startSunrise(DateTime now);
void startAlarm();
void dismissAlarm(DateTime now);

// Display
void updateClockDisplay(DateTime now);
void updateSleepDisplay(DateTime now);
void showReportScreen(uint8_t screen);
void drawMenuPage();

// Sunrise + alarm
void updateSunriseLED();
void updateAlarmMelody();
void playMelodyNote(uint8_t stage);
void stopAlarmSounds();

// Calculations
uint16_t calcSleepMinutes(uint8_t sh, uint8_t sm, uint8_t wh, uint8_t wm);
int16_t  calcSleepDebt();
void     calcConsistency(char *buf);
bool     isAlarmTime(DateTime now);
bool     isSunriseTime(DateTime now);

// Sensing
void     sampleDHT();
void     sampleLDR();

// Util
void     bootDiagnostic();


// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  pinMode(BUZZER_PIN,       OUTPUT);
  pinMode(RELAY_PIN,        OUTPUT);
  pinMode(SUNRISE_LED_PIN,  OUTPUT);
  pinMode(STATUS_LED_PIN,   OUTPUT);
  pinMode(JOYSTICK_BTN_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, LOW);
  analogWrite(SUNRISE_LED_PIN, 0);
  analogWrite(STATUS_LED_PIN,  0);
  noTone(BUZZER_PIN);

  Serial.begin(9600);

  Wire.begin();

  // Parallel LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print(F("DAWNTRACE v2.0"));
  lcd.setCursor(0, 1);
  lcd.print(F("Initializing..."));

  // RTC (software millis fallback — swap for RTC_DS1307 with real hardware)
  rtc.begin(DateTime(F(__DATE__), F(__TIME__)));
  Serial.println(F("RTC init"));

  dht.begin();

  initEEPROM();
  loadSettings();
  loadSleepLog();

  // Quick status LED flash
  analogWrite(STATUS_LED_PIN, 120);
  delay(400);
  analogWrite(STATUS_LED_PIN, 0);

  bootDiagnostic();

  lcd.clear();
  currentState = STATE_IDLE;
  lastDisplayUpdate = 0;
  Serial.println(F("DAWNTRACE ready — Phases 2-8"));
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
  // Always update input events first
  checkJoystick();

  DateTime now = rtc.now();

  // ── Global: check if alarm should trigger (works even without sleep mode) ──
  if (currentState == STATE_IDLE || currentState == STATE_SLEEP_MODE) {
    checkAlarmTrigger(now);
  }

  // ── Update sunrise LED if active ──
  if (sunriseActive) {
    updateSunriseLED();
  }

  // ── Update alarm melody if firing ──
  if (alarmFiring) {
    updateAlarmMelody();
  }

  // ── State machine ──
  switch (currentState) {

    case STATE_IDLE:
      handleIdleState();
      // Long-press joystick → enter sleep mode
      if (evtLongPress) {
        enterSleepMode(now);
      }
      // Short-press joystick → enter settings menu
      // (We use a dedicated "open settings" gesture: short press in IDLE)
      if (evtShortPress) {
        // Copy current settings into temp menu vars
        menuAlarmHour    = alarmHour;
        menuAlarmMin     = alarmMinute;
        menuSunriseDur   = sunriseDuration;
        menuTargetSleep  = targetSleepHours;
        menuAlarmEnabled = alarmEnabled;
        menuLdrThresh    = ldrThreshold;
        currentMenuPage  = MENU_ALARM_HOUR;
        currentState     = STATE_SETTINGS_MENU;
        lcd.clear();
        drawMenuPage();
      }
      break;

    case STATE_SLEEP_MODE:
      handleSleepState();
      // Long-press to manually exit sleep mode (emergency / testing)
      if (evtLongPress) {
        exitSleepMode(now);
      }
      break;

    case STATE_WAKE_SEQUENCE:
      handleWakeSequence();
      break;

    case STATE_ALARM_ACTIVE:
      handleAlarmActive();
      // Any joystick press dismisses alarm
      if (evtShortPress || evtLongPress) {
        dismissAlarm(now);
      }
      break;

    case STATE_POST_SLEEP_REPORT:
      handlePostSleepReport();
      // Short-press or right-tilt → advance report screen
      if (evtShortPress || evtJoyRight) {
        reportScreen++;
        reportScreenDirty = true;
        if (reportScreen >= REPORT_SCREENS) {
          saveSleepEntry();
          lcd.clear();
          lcd.print(F("  Data saved!   "));
          delay(1200);
          lcd.clear();
          currentState = STATE_IDLE;
          lastDisplayUpdate = 0;
        }
      }
      break;

    case STATE_SETTINGS_MENU:
      handleSettingsMenu();
      break;
  }
}

// ============================================================================
//  EEPROM
// ============================================================================
void initEEPROM() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
    Serial.println(F("First boot — writing EEPROM defaults"));
    EEPROM.update(EEPROM_MAGIC_ADDR,    EEPROM_MAGIC_VALUE);
    EEPROM.update(EEPROM_ALARM_HOUR,    7);
    EEPROM.update(EEPROM_ALARM_MIN,     0);
    EEPROM.update(EEPROM_ALARM_ENABLED, 1);
    EEPROM.update(EEPROM_SUNRISE_DUR,   SUNRISE_DEFAULT_MINUTES);
    EEPROM.update(EEPROM_TARGET_SLEEP,  SLEEP_DEBT_TARGET_HOURS);
    EEPROM.update(EEPROM_ALARM_VOLUME,  3);
    EEPROM.update(EEPROM_LDR_THRESH_H,  (uint8_t)(LDR_THRESHOLD >> 8));
    EEPROM.update(EEPROM_LDR_THRESH_L,  (uint8_t)(LDR_THRESHOLD & 0xFF));
    EEPROM.update(0x0009, 0);
    EEPROM.update(EEPROM_LOG_INDEX, 0);
    for (int i = 0; i < 240; i++) {
      EEPROM.update(EEPROM_LOG_START + i, 0xFF);
    }
  }
}

void loadSettings() {
  alarmHour      = EEPROM.read(EEPROM_ALARM_HOUR);
  alarmMinute    = EEPROM.read(EEPROM_ALARM_MIN);
  alarmEnabled   = (EEPROM.read(EEPROM_ALARM_ENABLED) == 1);
  sunriseDuration= EEPROM.read(EEPROM_SUNRISE_DUR);
  targetSleepHours = EEPROM.read(EEPROM_TARGET_SLEEP);
  alarmVolume    = EEPROM.read(EEPROM_ALARM_VOLUME);
  uint8_t ldrH   = EEPROM.read(EEPROM_LDR_THRESH_H);
  uint8_t ldrL   = EEPROM.read(EEPROM_LDR_THRESH_L);
  ldrThreshold   = ((uint16_t)ldrH << 8) | ldrL;

  // Validate
  if (alarmHour    > 23)  alarmHour    = 7;
  if (alarmMinute  > 59)  alarmMinute  = 0;
  if (sunriseDuration < 5 || sunriseDuration > 60) sunriseDuration = SUNRISE_DEFAULT_MINUTES;
  if (targetSleepHours < 4 || targetSleepHours > 12) targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
  if (alarmVolume  >  5)  alarmVolume  = 3;
  if (ldrThreshold == 0 || ldrThreshold > 1020) ldrThreshold = LDR_THRESHOLD;

  logIndex = EEPROM.read(EEPROM_LOG_INDEX);
  if (logIndex >= 30) logIndex = 0;

  Serial.print(F("Alarm: ")); Serial.print(alarmHour);
  Serial.print(F(":")); Serial.print(alarmMinute);
  Serial.print(F("  Sunrise: ")); Serial.print(sunriseDuration);
  Serial.print(F("min  Enabled:")); Serial.println(alarmEnabled);
}

void loadSleepLog() {
  for (uint8_t i = 0; i < 30; i++) {
    int addr = EEPROM_LOG_START + (i * 8);
    sleepLog[i].sleepHour  = EEPROM.read(addr + 0);
    sleepLog[i].sleepMin   = EEPROM.read(addr + 1);
    sleepLog[i].wakeHour   = EEPROM.read(addr + 2);
    sleepLog[i].wakeMin    = EEPROM.read(addr + 3);
    sleepLog[i].avgTemp    = EEPROM.read(addr + 4);
    sleepLog[i].avgHum     = EEPROM.read(addr + 5);
    sleepLog[i].lightEvents= EEPROM.read(addr + 6);
    sleepLog[i].dayOfWeek  = EEPROM.read(addr + 7);
  }
}

void saveSettings() {
  EEPROM.update(EEPROM_ALARM_HOUR,    alarmHour);
  EEPROM.update(EEPROM_ALARM_MIN,     alarmMinute);
  EEPROM.update(EEPROM_ALARM_ENABLED, alarmEnabled ? 1 : 0);
  EEPROM.update(EEPROM_SUNRISE_DUR,   sunriseDuration);
  EEPROM.update(EEPROM_TARGET_SLEEP,  targetSleepHours);
  EEPROM.update(EEPROM_ALARM_VOLUME,  alarmVolume);
  EEPROM.update(EEPROM_LDR_THRESH_H,  (uint8_t)(ldrThreshold >> 8));
  EEPROM.update(EEPROM_LDR_THRESH_L,  (uint8_t)(ldrThreshold & 0xFF));
  Serial.println(F("Settings saved to EEPROM"));
}

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

  // Mirror into RAM
  sleepLog[logIndex].sleepHour   = sleepStartHour;
  sleepLog[logIndex].sleepMin    = sleepStartMinute;
  sleepLog[logIndex].wakeHour    = wakeHour;
  sleepLog[logIndex].wakeMin     = wakeMinute;
  sleepLog[logIndex].avgTemp     = avgTemp;
  sleepLog[logIndex].avgHum      = avgHum;
  sleepLog[logIndex].lightEvents = lightEventCount;
  sleepLog[logIndex].dayOfWeek   = now.dayOfTheWeek();

  logIndex = (logIndex + 1) % 30;
  EEPROM.update(EEPROM_LOG_INDEX, logIndex);
  Serial.println(F("Sleep entry saved"));
}

// ============================================================================
//  JOYSTICK INPUT — debounced button + axis events
// ============================================================================
void checkJoystick() {
  // Reset events
  evtShortPress = false;
  evtLongPress  = false;
  evtJoyLeft    = false;
  evtJoyRight   = false;

  // ── Button ──
  bool reading = digitalRead(JOYSTICK_BTN_PIN);  // active-low

  if (reading != lastBtnReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading == LOW) {
      // Pressed
      if (!btnWasPressed) {
        btnWasPressed       = true;
        btnLongPressHandled = false;
        btnPressStart       = millis();
      } else {
        // Being held
        if (!btnLongPressHandled &&
            (millis() - btnPressStart) >= JOYSTICK_HOLD_MS) {
          evtLongPress        = true;
          btnLongPressHandled = true;
        }
      }
    } else {
      // Released
      if (btnWasPressed && !btnLongPressHandled) {
        // Was a short press (released before long-press threshold)
        if ((millis() - btnPressStart) >= JOYSTICK_SHORT_MS) {
          evtShortPress = true;
        }
      }
      btnWasPressed       = false;
      btnLongPressHandled = false;
    }
  }
  lastBtnReading = reading;

  // ── Joystick axis (one event per tilt, must return to centre) ──
  int xVal = analogRead(JOYSTICK_VRX_PIN);

  if (xVal < JOY_LEFT_THRESHOLD) {
    if (!joyWasLeft) {
      evtJoyLeft = true;
      joyWasLeft = true;
    }
    joyWasRight = false;
  } else if (xVal > JOY_RIGHT_THRESHOLD) {
    if (!joyWasRight) {
      evtJoyRight = true;
      joyWasRight = true;
    }
    joyWasLeft = false;
  } else {
    // In centre dead-band
    joyWasLeft  = false;
    joyWasRight = false;
  }
}

// ============================================================================
//  STATE HANDLERS
// ============================================================================

void handleIdleState() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    updateClockDisplay(rtc.now());
    // Gentle breathing on status LED
    uint8_t bv = (uint8_t)(35.0f * (1.0f + sinf((float)millis() / 2000.0f)));
    analogWrite(STATUS_LED_PIN, bv);
  }
}

void handleSleepState() {
  unsigned long now = millis();

  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;
    updateSleepDisplay(rtc.now());
  }

  if (now - lastDHTSample >= DHT_INTERVAL) {
    lastDHTSample = now;
    sampleDHT();
  }

  if (now - lastLDRSample >= LDR_INTERVAL) {
    lastLDRSample = now;
    sampleLDR();
  }

  analogWrite(STATUS_LED_PIN, 0);
}

// ── Wake Sequence: show minimal info while sunrise runs ──
void handleWakeSequence() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    DateTime now = rtc.now();
    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "  %02d:%02d  RISING", now.hour(), now.minute());
    uint8_t pct = 0;
    if (sunriseTotalMs > 0) {
      unsigned long elapsed = millis() - sunriseStartMs;
      if (elapsed >= sunriseTotalMs) elapsed = sunriseTotalMs;
      pct = (uint8_t)((elapsed * 100UL) / sunriseTotalMs);
    }
    snprintf(line2, sizeof(line2), "Sunrise: %3d%%   ", pct);
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
  }

  // Check if sunrise is complete → transition to full alarm
  if (sunriseActive && (millis() - sunriseStartMs) >= sunriseTotalMs) {
    sunriseActive = false;
    analogWrite(SUNRISE_LED_PIN, 255);  // Hold at full brightness
    startAlarm();
  }
}

// ── Alarm Active: full brightness, escalating melody ──
void handleAlarmActive() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    DateTime now = rtc.now();
    char line1[17];
    snprintf(line1, sizeof(line1), "  %02d:%02d  ALARM!", now.hour(), now.minute());
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1);
    // Flash message
    if ((millis() / 500) % 2 == 0) {
      lcd.print(F("Click joystick! "));
    } else {
      lcd.print(F("                "));
    }
  }
}

void handlePostSleepReport() {
  if (reportScreenDirty) {
    reportScreenDirty = false;
    showReportScreen(reportScreen);
  }
}

// ============================================================================
//  SETTINGS MENU (Phase 6 — joystick only, no keypad)
// ============================================================================
// Navigation:
//   Left/Right  : change value for current setting
//   Short press : next setting page
//   Long press  : save & exit (from any page)
//   Reaching MENU_SAVE_EXIT + short press : save & exit
//
void handleSettingsMenu() {
  bool redraw = false;

  // ── Value adjustment (left/right tilt) ──
  if (evtJoyLeft || evtJoyRight) {
    int dir = evtJoyRight ? 1 : -1;
    switch (currentMenuPage) {
      case MENU_ALARM_HOUR:
        menuAlarmHour = (uint8_t)((menuAlarmHour + dir + 24) % 24);
        break;
      case MENU_ALARM_MIN:
        // Snap to 5-minute increments for easier setting
        menuAlarmMin = (uint8_t)((menuAlarmMin + dir * 5 + 60) % 60);
        break;
      case MENU_SUNRISE_DUR:
        menuSunriseDur = constrain((int)menuSunriseDur + dir * 5, 5, 60);
        break;
      case MENU_TARGET_SLEEP:
        menuTargetSleep = constrain((int)menuTargetSleep + dir, 4, 12);
        break;
      case MENU_ALARM_TOGGLE:
        menuAlarmEnabled = !menuAlarmEnabled;
        break;
      case MENU_LDR_THRESH:
        menuLdrThresh = constrain((int)menuLdrThresh + dir * 50, 50, 950);
        break;
      case MENU_SAVE_EXIT:
        // Nothing to adjust here
        break;
      default: break;
    }
    redraw = true;
  }

  // ── Short press: advance to next page ──
  if (evtShortPress) {
    if (currentMenuPage == MENU_SAVE_EXIT) {
      // Commit changes
      alarmHour      = menuAlarmHour;
      alarmMinute    = menuAlarmMin;
      sunriseDuration= menuSunriseDur;
      targetSleepHours = menuTargetSleep;
      alarmEnabled   = menuAlarmEnabled;
      ldrThreshold   = menuLdrThresh;
      saveSettings();

      // Reset sunrise trigger so new time takes effect tonight
      lastAlarmFiredDay = 255;

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(F("  Settings saved"));
      lcd.setCursor(0, 1); lcd.print(F("  Alarm: "));
      lcd.print(alarmHour < 10 ? "0" : ""); lcd.print(alarmHour);
      lcd.print(F(":")); lcd.print(alarmMinute < 10 ? "0" : ""); lcd.print(alarmMinute);
      delay(1500);
      lcd.clear();
      currentState = STATE_IDLE;
      lastDisplayUpdate = 0;
      return;
    }
    currentMenuPage = (MenuPage)((uint8_t)currentMenuPage + 1);
    redraw = true;
  }

  // ── Long press: discard & exit ──
  if (evtLongPress) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("  Cancelled     "));
    delay(800);
    lcd.clear();
    currentState = STATE_IDLE;
    lastDisplayUpdate = 0;
    return;
  }

  if (redraw) {
    drawMenuPage();
  }
}

// Draw the current menu page onto the LCD
void drawMenuPage() {
  lcd.clear();
  switch (currentMenuPage) {

    case MENU_ALARM_HOUR:
      lcd.setCursor(0, 0); lcd.print(F("Alarm Hour      "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuAlarmHour < 10 ? "0" : ""); lcd.print(menuAlarmHour);
      lcd.print(F(":"));
      lcd.print(menuAlarmMin < 10 ? "0" : ""); lcd.print(menuAlarmMin);
      lcd.print(F(" >  [ok=next]"));
      break;

    case MENU_ALARM_MIN:
      lcd.setCursor(0, 0); lcd.print(F("Alarm Minute+5  "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuAlarmHour < 10 ? "0" : ""); lcd.print(menuAlarmHour);
      lcd.print(F(":"));
      lcd.print(menuAlarmMin < 10 ? "0" : ""); lcd.print(menuAlarmMin);
      lcd.print(F(" >  [ok=next]"));
      break;

    case MENU_SUNRISE_DUR:
      lcd.setCursor(0, 0); lcd.print(F("Sunrise (min)   "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuSunriseDur);
      lcd.print(F(" min   >  [ok]"));
      break;

    case MENU_TARGET_SLEEP:
      lcd.setCursor(0, 0); lcd.print(F("Target Sleep    "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuTargetSleep);
      lcd.print(F(" hrs   >  [ok]"));
      break;

    case MENU_ALARM_TOGGLE:
      lcd.setCursor(0, 0); lcd.print(F("Alarm ON/OFF    "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuAlarmEnabled ? F("ON ") : F("OFF"));
      lcd.print(F("   >  [ok]"));
      break;

    case MENU_LDR_THRESH:
      lcd.setCursor(0, 0); lcd.print(F("Light Threshold "));
      lcd.setCursor(0, 1);
      lcd.print(F("< "));
      lcd.print(menuLdrThresh);
      lcd.print(F("  >  [ok]  "));
      break;

    case MENU_SAVE_EXIT:
      lcd.setCursor(0, 0); lcd.print(F("SAVE & EXIT?    "));
      lcd.setCursor(0, 1); lcd.print(F("[ok]=save[hold]=discard"));
      break;

    default: break;
  }
}

// ============================================================================
//  ALARM TRIGGER LOGIC (Phase 8 — works regardless of sleep mode)
// ============================================================================

// Returns true if we are within the sunrise window (before alarm time)
bool isSunriseTime(DateTime now) {
  if (!alarmEnabled) return false;

  // Convert alarm time to minutes since midnight
  int16_t alarmTotalMin = (int16_t)alarmHour * 60 + (int16_t)alarmMinute;
  int16_t sunriseStartMin = alarmTotalMin - (int16_t)sunriseDuration;

  // Current time in minutes since midnight
  int16_t nowMin = (int16_t)now.hour() * 60 + (int16_t)now.minute();

  // Handle midnight wrap for sunrise start
  if (sunriseStartMin < 0) sunriseStartMin += 1440;

  // Are we in the window [sunriseStartMin, alarmTotalMin)?
  // Need to handle the case where the window crosses midnight
  if (sunriseStartMin < alarmTotalMin) {
    return (nowMin >= sunriseStartMin && nowMin < alarmTotalMin);
  } else {
    // Crosses midnight
    return (nowMin >= sunriseStartMin || nowMin < alarmTotalMin);
  }
}

bool isAlarmTime(DateTime now) {
  if (!alarmEnabled) return false;
  return (now.hour() == alarmHour && now.minute() == alarmMinute);
}

void checkAlarmTrigger(DateTime now) {
  // Prevent re-triggering on the same day
  if (now.day() == lastAlarmFiredDay) return;

  // Check sunrise window first
  if (!sunriseActive && !alarmFiring &&
      isSunriseTime(now) &&
      currentState != STATE_ALARM_ACTIVE &&
      currentState != STATE_WAKE_SEQUENCE) {
    startSunrise(now);
  }

  // Check exact alarm time (also fires if sunrise was skipped / disabled)
  if (!alarmFiring &&
      isAlarmTime(now) &&
      currentState != STATE_ALARM_ACTIVE &&
      currentState != STATE_WAKE_SEQUENCE) {
    // Ensure LEDs are at full if sunrise wasn't running
    analogWrite(SUNRISE_LED_PIN, 255);
    sunriseActive = false;
    startAlarm();
    lastAlarmFiredDay = now.day();
  }
}

void startSunrise(DateTime now) {
  sunriseActive   = true;
  sunriseStartMs  = millis();
  sunriseTotalMs  = (unsigned long)sunriseDuration * 60UL * 1000UL;

  // Relay off (lamp control is at alarm time, not sunrise)
  digitalWrite(RELAY_PIN, LOW);

  // Transition to wake sequence state
  currentState = STATE_WAKE_SEQUENCE;
  lcd.clear();

  Serial.print(F("Sunrise started: "));
  Serial.print(sunriseDuration);
  Serial.println(F(" min"));
}

void startAlarm() {
  alarmFiring    = true;
  alarmStartMs   = millis();
  melodyNoteIndex = 0;
  lastNoteTime   = 0;

  // Turn on relay (external lamp)
  digitalWrite(RELAY_PIN, HIGH);

  analogWrite(SUNRISE_LED_PIN, 255);

  currentState = STATE_ALARM_ACTIVE;
  lcd.clear();

  Serial.println(F("ALARM ACTIVE"));
}

void dismissAlarm(DateTime now) {
  alarmFiring   = false;
  sunriseActive = false;
  lastAlarmFiredDay = now.day();   // mark as handled for today

  stopAlarmSounds();
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  // Record wake time
  wakeHour   = now.hour();
  wakeMinute = now.minute();

  if (inSleepMode) {
    inSleepMode = false;
    sleepDurationMin = calcSleepMinutes(
        sleepStartHour, sleepStartMinute, wakeHour, wakeMinute);
  } else {
    // Alarm fired without sleep mode — still show report, mark no sleep logged
    sleepWasLogged   = false;
    sleepDurationMin = 0;
    sleepStartHour   = 0;
    sleepStartMinute = 0;
  }

  currentState     = STATE_POST_SLEEP_REPORT;
  reportScreen     = 0;
  reportScreenDirty = true;
  lastDisplayUpdate = 0;

  Serial.println(F("Alarm dismissed → report"));
}

// ============================================================================
//  SUNRISE LED — cosine ease-in curve  (Phase 5 — Step 5.2)
// ============================================================================
void updateSunriseLED() {
  unsigned long elapsed = millis() - sunriseStartMs;
  if (elapsed > sunriseTotalMs) elapsed = sunriseTotalMs;

  float progress = (float)elapsed / (float)sunriseTotalMs;
  // Cosine ease-in: slow start, accelerating towards wake time
  float brightness = 255.0f * (1.0f - cosf(progress * PI)) / 2.0f;
  uint8_t pwmVal = (uint8_t)constrain((int)brightness, 0, 255);
  analogWrite(SUNRISE_LED_PIN, pwmVal);
}

// ============================================================================
//  ALARM MELODY — 3 stages  (Phase 5 — Step 5.3)
// ============================================================================
// Stage 1 (0–2 min):  C5→E5→G5 gentle chime every 10 seconds
// Stage 2 (2–5 min):  same notes, every 5 seconds, longer
// Stage 3 (5+ min):   rapid G5→A5→B5 continuous at ~2 Hz
//
// Uses millis() exclusively — no delay(), stays non-blocking.

// Stage 1 sequence: {freq, duration_ms}
const uint16_t stage1Notes[][2] = {
  {NOTE_C5, 200}, {NOTE_E5, 200}, {NOTE_G5, 400}
};
const uint8_t  STAGE1_NOTE_COUNT = 3;
const uint16_t STAGE1_GAP_MS     = 10000;  // 10 sec between chimes

// Stage 2 sequence
const uint16_t stage2Notes[][2] = {
  {NOTE_C5, 250}, {NOTE_E5, 250}, {NOTE_G5, 500}
};
const uint8_t  STAGE2_NOTE_COUNT = 3;
const uint16_t STAGE2_GAP_MS     = 5000;   // 5 sec between chimes

// Stage 3 sequence (loop continuously)
const uint16_t stage3Notes[][2] = {
  {NOTE_G5, 100}, {NOTE_A5, 100}, {NOTE_B5, 100}
};
const uint8_t  STAGE3_NOTE_COUNT = 3;

struct MelodyState {
  uint8_t  noteIdx;       // which note in current stage sequence
  uint8_t  stage;         // 1, 2, or 3
  bool     inGap;         // true = waiting between chimes (stages 1&2)
  unsigned long nextTime; // millis() when next action is due
} melody;

void updateAlarmMelody() {
  unsigned long elapsed = millis() - alarmStartMs;

  // Determine current stage
  uint8_t newStage;
  if (elapsed < STAGE2_MS)      newStage = 1;
  else if (elapsed < STAGE3_MS) newStage = 2;
  else                           newStage = 3;

  // Stage change — reset sequence
  if (newStage != melody.stage) {
    melody.stage    = newStage;
    melody.noteIdx  = 0;
    melody.inGap    = false;
    melody.nextTime = millis();
    noTone(BUZZER_PIN);
    Serial.print(F("Alarm Stage ")); Serial.println(newStage);
  }

  if (millis() < melody.nextTime) return;  // not time yet

  if (melody.stage == 1) {
    if (melody.inGap) {
      // Gap over → start next chime from note 0
      melody.inGap   = false;
      melody.noteIdx = 0;
    }
    // Play next note
    uint16_t freq = stage1Notes[melody.noteIdx][0];
    uint16_t dur  = stage1Notes[melody.noteIdx][1];
    tone(BUZZER_PIN, freq, dur);
    melody.noteIdx++;
    if (melody.noteIdx >= STAGE1_NOTE_COUNT) {
      // Chime done → gap
      melody.inGap   = true;
      melody.noteIdx = 0;
      melody.nextTime = millis() + STAGE1_GAP_MS;
    } else {
      // Next note immediately after current duration
      melody.nextTime = millis() + dur + 20;  // +20ms tiny gap between notes
    }
  }
  else if (melody.stage == 2) {
    if (melody.inGap) {
      melody.inGap   = false;
      melody.noteIdx = 0;
    }
    uint16_t freq = stage2Notes[melody.noteIdx][0];
    uint16_t dur  = stage2Notes[melody.noteIdx][1];
    tone(BUZZER_PIN, freq, dur);
    melody.noteIdx++;
    if (melody.noteIdx >= STAGE2_NOTE_COUNT) {
      melody.inGap    = true;
      melody.noteIdx  = 0;
      melody.nextTime = millis() + STAGE2_GAP_MS;
    } else {
      melody.nextTime = millis() + dur + 20;
    }
  }
  else {
    // Stage 3 — continuous rapid loop
    uint16_t freq = stage3Notes[melody.noteIdx][0];
    uint16_t dur  = stage3Notes[melody.noteIdx][1];
    tone(BUZZER_PIN, freq, dur);
    melody.noteIdx = (melody.noteIdx + 1) % STAGE3_NOTE_COUNT;
    melody.nextTime = millis() + dur + 10;
  }
}

void stopAlarmSounds() {
  noTone(BUZZER_PIN);
  melody.stage    = 0;
  melody.noteIdx  = 0;
  melody.inGap    = false;
  melody.nextTime = 0;
}

// ============================================================================
//  SLEEP MODE ENTER / EXIT  (Phase 3)
// ============================================================================
void enterSleepMode(DateTime now) {
  if (inSleepMode) return;
  inSleepMode      = true;
  sleepWasLogged   = true;
  currentState     = STATE_SLEEP_MODE;

  sleepStartHour   = now.hour();
  sleepStartMinute = now.minute();
  sleepModeEnteredAt = millis();

  tempSum = humSum = sampleCount = 0;
  avgTemp = avgHum = lightEventCount = 0;
  lightWasAbove = false;

  lastDHTSample = millis() - DHT_INTERVAL;  // force immediate first sample
  lastLDRSample = millis() - LDR_INTERVAL;

  lcd.clear();
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  Serial.print(F("Sleep ON @ ")); Serial.print(sleepStartHour);
  Serial.print(F(":")); Serial.println(sleepStartMinute);
}

void exitSleepMode(DateTime now) {
  if (!inSleepMode) return;
  inSleepMode = false;

  wakeHour   = now.hour();
  wakeMinute = now.minute();
  sleepDurationMin = calcSleepMinutes(
      sleepStartHour, sleepStartMinute, wakeHour, wakeMinute);

  lcd.clear();
  currentState      = STATE_POST_SLEEP_REPORT;
  reportScreen      = 0;
  reportScreenDirty = true;

  Serial.print(F("Sleep OFF @ ")); Serial.print(wakeHour);
  Serial.print(F(":")); Serial.print(wakeMinute);
  Serial.print(F(" — ")); Serial.print(sleepDurationMin / 60);
  Serial.print(F("h ")); Serial.print(sleepDurationMin % 60);
  Serial.println(F("min"));
}

// ============================================================================
//  ENVIRONMENTAL SAMPLING  (Phase 4)
// ============================================================================
void sampleDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println(F("DHT read failed"));
    return;
  }
  tempSum += (unsigned long)t;
  humSum  += (unsigned long)h;
  sampleCount++;
  avgTemp = (uint8_t)(tempSum / sampleCount);
  avgHum  = (uint8_t)(humSum  / sampleCount);
  Serial.print(F("DHT T=")); Serial.print(t,1);
  Serial.print(F(" H=")); Serial.print(h,1);
  Serial.print(F(" avg(")); Serial.print(sampleCount); Serial.println(F(")"));
}

void sampleLDR() {
  int ldrVal = analogRead(LDR_PIN);
  // Ignore during grace period
  if ((millis() - sleepModeEnteredAt) < LDR_GRACE_PERIOD) {
    Serial.print(F("LDR=")); Serial.print(ldrVal); Serial.println(F(" (grace)"));
    return;
  }
  bool currentlyAbove = (ldrVal > (int)ldrThreshold);
  if (currentlyAbove && !lightWasAbove) {
    lightEventCount++;
    Serial.print(F("Light event #")); Serial.println(lightEventCount);
  }
  lightWasAbove = currentlyAbove;
}

// ============================================================================
//  DISPLAY FUNCTIONS
// ============================================================================
void updateClockDisplay(DateTime now) {
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s",
           now.hour(), now.minute(), now.second(),
           dayNames[now.dayOfTheWeek()]);
  if (alarmEnabled) {
    snprintf(line2, sizeof(line2), "%02d/%02d/%02d A%02d:%02d",
             now.day(), now.month(), now.year() % 100,
             alarmHour, alarmMinute);
  } else {
    snprintf(line2, sizeof(line2), "%02d/%02d/%02d A:OFF",
             now.day(), now.month(), now.year() % 100);
  }
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void updateSleepDisplay(DateTime dt) {
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "   %02d:%02d  ZZZ  ", dt.hour(), dt.minute());
  if (sampleCount > 0) {
    snprintf(line2, sizeof(line2), "%dC %d%% L:%d     ", avgTemp, avgHum, lightEventCount);
  } else {
    strcpy(line2, "Sensing...      ");
  }
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void showReportScreen(uint8_t screen) {
  lcd.clear();
  char line1[17], line2[17];

  switch (screen) {
    case 0: {
      uint16_t h = sleepDurationMin / 60;
      uint16_t m = sleepDurationMin % 60;
      if (sleepWasLogged) {
        snprintf(line1, sizeof(line1), "Slept: %dh %02dmin", h, m);
        snprintf(line2, sizeof(line2), "Wake: %02d:%02d      ", wakeHour, wakeMinute);
      } else {
        strcpy(line1, "Slept: not logged");
        snprintf(line2, sizeof(line2), "Wake: %02d:%02d      ", wakeHour, wakeMinute);
      }
      break;
    }
    case 1:
      if (sampleCount > 0) {
        snprintf(line1, sizeof(line1), "Avg Temp: %d C  ", avgTemp);
        snprintf(line2, sizeof(line2), "Avg Hum:  %d%%   ", avgHum);
      } else {
        strcpy(line1, "Avg Temp: -- C  ");
        strcpy(line2, "Avg Hum:  --%   ");
      }
      break;
    case 2:
      snprintf(line1, sizeof(line1), "Light Events: %d ", lightEventCount);
      if      (lightEventCount == 0) strcpy(line2, "Night: Dark     ");
      else if (lightEventCount <= 3) strcpy(line2, "Night: Some     ");
      else                           strcpy(line2, "Night: Bright!  ");
      break;
    case 3: {
      int16_t debt  = calcSleepDebt();
      uint16_t dh   = debt / 60;
      uint16_t dm   = debt % 60;
      if (debt == 0) strcpy(line1, "Debt: NONE      ");
      else           snprintf(line1, sizeof(line1), "Debt: %dh %02dmin ", dh, dm);
      char pat[12];
      calcConsistency(pat);
      snprintf(line2, sizeof(line2), "Pattern: %-7s", pat);
      break;
    }
    case 4:
      strcpy(line1, "  Log saved!    ");
      strcpy(line2, " Tilt > or OK   ");
      break;
    default:
      strcpy(line1, "                ");
      strcpy(line2, "                ");
      break;
  }

  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);

  Serial.print(F("Report ")); Serial.print(screen+1);
  Serial.print(F("/")); Serial.print(REPORT_SCREENS);
  Serial.print(F(": ")); Serial.println(line1);
}

// ============================================================================
//  SLEEP CALCULATIONS
// ============================================================================
uint16_t calcSleepMinutes(uint8_t sh, uint8_t sm, uint8_t wh, uint8_t wm) {
  int16_t s = (int16_t)sh * 60 + (int16_t)sm;
  int16_t w = (int16_t)wh * 60 + (int16_t)wm;
  int16_t d = w - s;
  if (d < 0) d += 1440;
  return (uint16_t)d;
}

int16_t calcSleepDebt() {
  int16_t totalDebt = 0;
  int16_t targetMin = (int16_t)targetSleepHours * 60;
  for (uint8_t i = 0; i < 7; i++) {
    int8_t idx = (int8_t)logIndex - 1 - (int8_t)i;
    if (idx < 0) idx += 30;
    if (sleepLog[idx].sleepHour == 0xFF) continue;
    uint16_t actual = calcSleepMinutes(
        sleepLog[idx].sleepHour, sleepLog[idx].sleepMin,
        sleepLog[idx].wakeHour,  sleepLog[idx].wakeMin);
    totalDebt += (targetMin - (int16_t)actual);
  }
  if (totalDebt < 0) totalDebt = 0;
  return totalDebt;
}

void calcConsistency(char *buf) {
  int16_t minStart =  32767;
  int16_t maxStart = -32767;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < 7; i++) {
    int8_t idx = (int8_t)logIndex - 1 - (int8_t)i;
    if (idx < 0) idx += 30;
    if (sleepLog[idx].sleepHour == 0xFF) continue;
    int16_t sm = (int16_t)sleepLog[idx].sleepHour * 60 + sleepLog[idx].sleepMin;
    if (sm < 120) sm += 1440;
    if (sm < minStart) minStart = sm;
    if (sm > maxStart) maxStart = sm;
    valid++;
  }
  if (valid < 2) { strcpy(buf, "N/A"); return; }
  int16_t spread = maxStart - minStart;
  if      (spread < 30) strcpy(buf, "CONSISTENT");
  else if (spread < 60) strcpy(buf, "MODERATE");
  else                  strcpy(buf, "IRREGULAR");
}

// ============================================================================
//  BOOT DIAGNOSTIC  (quick hardware check)
// ============================================================================
void bootDiagnostic() {
  lcd.clear();
  lcd.print(F("Hardware check.."));
  delay(600);

  // Relay click
  lcd.setCursor(0,1); lcd.print(F("Relay...        "));
  digitalWrite(RELAY_PIN, HIGH); delay(300);
  digitalWrite(RELAY_PIN, LOW);  delay(200);

  // Buzzer two-tone
  lcd.setCursor(0,1); lcd.print(F("Buzzer...       "));
  tone(BUZZER_PIN, NOTE_C5, 150); delay(200);
  tone(BUZZER_PIN, NOTE_G5, 150); delay(200);
  noTone(BUZZER_PIN);

  // LED ramp
  lcd.setCursor(0,1); lcd.print(F("Sunrise LEDs... "));
  for (int i = 0; i <= 255; i += 3) {
    analogWrite(SUNRISE_LED_PIN, i);
    delay(6);
  }
  for (int i = 255; i >= 0; i -= 3) {
    analogWrite(SUNRISE_LED_PIN, i);
    delay(6);
  }
  analogWrite(SUNRISE_LED_PIN, 0);

  // Status LED
  lcd.setCursor(0,1); lcd.print(F("Status LED...   "));
  analogWrite(STATUS_LED_PIN, 120); delay(400);
  analogWrite(STATUS_LED_PIN, 0);

  // Sensors
  lcd.clear();
  lcd.print(F("Sensors...      "));
  delay(800);
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int   ldr = analogRead(LDR_PIN);

  lcd.clear();
  if (isnan(t) || isnan(h)) {
    lcd.print(F("DHT11: ERROR    "));
  } else {
    lcd.print(F("T:")); lcd.print(t,1);
    lcd.print(F("C H:")); lcd.print(h,0); lcd.print(F("%"));
  }
  lcd.setCursor(0,1);
  lcd.print(F("LDR:")); lcd.print(ldr);
  lcd.print(F("  thr:")); lcd.print(ldrThreshold);
  delay(3000);
}
