// ============================================================================
// DAWNTRACE — Bedside Sleep Companion
// Firmware: Phases 2–8 + Phase 9 (IR Remote)
//
//   Phase 2:  Clock + Display (RTC_Millis, LCD parallel)
//   Phase 3:  Sleep Logging, debt calc, consistency score
//   Phase 4:  Environmental Sensing (DHT11 + LDR)
//   Phase 5:  Sunrise curve + 3-stage alarm melody + relay
//   Phase 6:  IR Remote Settings Menu (replaces joystick entirely)
//   Phase 7:  Full EEPROM persistence for settings
//   Phase 8:  Full state-machine integration & edge cases
//   Phase 9:  IR Remote input, snooze, direct-digit alarm entry
//
// Hardware:   ELEGOO Uno R3
//             LCD1602 PARALLEL  RS=8 EN=9 D4=10 D5=11 D6=12 D7=13
//             DS1307 RTC on I2C (A4/A5)
//             DHT11 on D2
//             Passive Buzzer on D3
//             5V Relay on D4
//             Sunrise LEDs via PN2222 on D5 (PWM)
//             Red Status LED on D6
//             IR Receiver (VS1838B) SIGNAL on D7   ← replaces joystick
//             LDR voltage divider on A2
//
// PIN CHANGE vs joystick build:
//   D7  was joystick SW  →  now IR receiver signal pin
//   A0  was joystick VRx →  now unused / free
//   Wiring: IR receiver  S → D7 | VCC → 5V | GND → GND
//
// LIBRARY:  IRremote v4.x   (z3t0/IRremote in platformio.ini)
//           All other libs identical to original source.
// ============================================================================

// IRremote v4 requires these before the include
#define IR_RECEIVE_PIN        7
#define DECODE_NEC               // ELEGOO remote uses NEC protocol
#define NO_LED_FEEDBACK_CODE     // saves flash — disable IR blink on pin 13
#define IR_NO_SEND               // Prevent Timer 2 from hijacking BUZZER_PIN (Pin 3)!
                                 // (pin 13 is our LCD D7 line, must not blink)

#include <Arduino.h>
#include <IRremote.hpp>          // v4.x — .hpp not .h
#include <DHT.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <RTClib.h>
#include <Wire.h>
#include <math.h>

// ============================================================================
//  PIN DEFINITIONS
// ============================================================================
#define DHT_PIN          2
#define BUZZER_PIN       3
#define RELAY_PIN        4
#define SUNRISE_LED_PIN  5
#define STATUS_LED_PIN   6
// D7  = IR_RECEIVE_PIN  (defined above, claimed by IRremote library)
// LCD parallel: RS=8, EN=9, D4=10, D5=11, D6=12, D7=13
#define LDR_PIN          A2

// ============================================================================
//  ELEGOO REMOTE — NEC hex codes
//  If any button is wrong, open Serial Monitor (9600 baud) and press it;
//  the sketch prints every received raw code.  Update the define and re-upload.
// ============================================================================
#define IR_BTN_UP     0xFF629D
#define IR_BTN_DOWN   0xFF22DD
#define IR_BTN_LEFT   0xFF02FD
#define IR_BTN_RIGHT  0xFFC23D
#define IR_BTN_OK     0xFF38C7
#define IR_BTN_STAR   0xFFA25D   // *
#define IR_BTN_HASH   0xFFE21D   // #
#define IR_BTN_0      0xFF52AD
#define IR_BTN_1      0xFF6897
#define IR_BTN_2      0xFF9867
#define IR_BTN_3      0xFFB04F
#define IR_BTN_4      0xFF30CF
#define IR_BTN_5      0xFF18E7
#define IR_BTN_6      0xFF7A85
#define IR_BTN_7      0xFF10EF
#define IR_BTN_8      0xFF42BD
#define IR_BTN_9      0xFF4AB5
#define IR_REPEAT     0xFFFFFFFF  // NEC auto-repeat while held

// Minimum gap between accepted repeat signals (ms)
#define IR_REPEAT_GAP_MS  300UL

// ============================================================================
//  CONFIGURATION CONSTANTS
// ============================================================================
#define SUNRISE_DEFAULT_MINUTES  20
#define SLEEP_DEBT_TARGET_HOURS   8
#define LDR_THRESHOLD           400

#define SNOOZE_5_MIN   5
#define SNOOZE_10_MIN 10

// Alarm melody note frequencies (Hz)
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988

#define DHT_TYPE DHT11

// ============================================================================
//  EEPROM MEMORY MAP  (identical to original)
// ============================================================================
#define EEPROM_MAGIC_ADDR    0x0000
#define EEPROM_ALARM_HOUR    0x0001
#define EEPROM_ALARM_MIN     0x0002
#define EEPROM_ALARM_ENABLED 0x0003
#define EEPROM_SUNRISE_DUR   0x0004
#define EEPROM_TARGET_SLEEP  0x0005
#define EEPROM_ALARM_VOLUME  0x0006
#define EEPROM_LDR_THRESH_H  0x0007
#define EEPROM_LDR_THRESH_L  0x0008
// 0x0009 reserved
#define EEPROM_LOG_START     0x000A  // 30 × 8 = 240 bytes
#define EEPROM_LOG_INDEX     0x00FA
#define EEPROM_MAGIC_VALUE   0xDA

// ============================================================================
//  STATE MACHINE
// ============================================================================
enum DeviceState {
  STATE_IDLE,
  STATE_SLEEP_MODE,
  STATE_WAKE_SEQUENCE,
  STATE_ALARM_ACTIVE,
  STATE_POST_SLEEP_REPORT,
  STATE_SETTINGS_MENU,
  STATE_SNOOZE            // new in Phase 9
};

enum MenuPage {
  MENU_ALARM_HOUR  = 0,
  MENU_ALARM_MIN,
  MENU_SUNRISE_DUR,
  MENU_TARGET_SLEEP,
  MENU_ALARM_TOGGLE,
  MENU_LDR_THRESH,
  MENU_SAVE_EXIT,
  MENU_PAGE_COUNT
};

// ============================================================================
//  GLOBAL OBJECTS
// ============================================================================
LiquidCrystal lcd(8, 9, 10, 11, 12, 13);   // parallel, identical to original
RTC_DS1307    rtc;
DHT           dht(DHT_PIN, DHT_TYPE);

// ============================================================================
//  SETTINGS  (loaded from / saved to EEPROM)
// ============================================================================
uint8_t  alarmHour        = 7;
uint8_t  alarmMinute      = 0;
bool     alarmEnabled     = true;
uint8_t  sunriseDuration  = SUNRISE_DEFAULT_MINUTES;
uint8_t  targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
uint8_t  alarmVolume      = 3;
uint16_t ldrThreshold     = LDR_THRESHOLD;

// Temp copies used inside the settings menu
uint8_t  menuAlarmHour;
uint8_t  menuAlarmMin;
uint8_t  menuSunriseDur;
uint8_t  menuTargetSleep;
bool     menuAlarmEnabled;
uint16_t menuLdrThresh;

// Direct digit-entry state (alarm hour / minute pages only)
uint8_t  digitFirst = 255;   // 255 = no digit pending

// ============================================================================
//  STATE VARIABLES
// ============================================================================
DeviceState currentState    = STATE_IDLE;
MenuPage    currentMenuPage = MENU_ALARM_HOUR;

unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000UL;

// ============================================================================
//  IR INPUT — event flags  (set in readIR(), consumed in loop/handlers)
// ============================================================================
bool    evtUp       = false;
bool    evtDown     = false;
bool    evtLeft     = false;
bool    evtRight    = false;
bool    evtOK       = false;
bool    evtStar     = false;
bool    evtHash     = false;
bool    evtDigit    = false;
uint8_t evtDigitVal = 0;

uint32_t lastIRCode    = 0;
uint32_t lastIRTime    = 0;

// ============================================================================
//  SLEEP TRACKING
// ============================================================================
bool     inSleepMode      = false;
uint8_t  sleepStartHour   = 0;
uint8_t  sleepStartMinute = 0;
uint8_t  wakeHour         = 0;
uint8_t  wakeMinute       = 0;
uint16_t sleepDurationMin = 0;
bool     sleepWasLogged   = false;

// ============================================================================
//  ENVIRONMENTAL SENSING
// ============================================================================
unsigned long lastDHTSample      = 0;
unsigned long lastLDRSample      = 0;
const unsigned long DHT_INTERVAL  = 120000UL;
const unsigned long LDR_INTERVAL  =  30000UL;
unsigned long sleepModeEnteredAt  = 0;
const unsigned long LDR_GRACE_PERIOD = 300000UL;

unsigned long tempSum    = 0;
unsigned long humSum     = 0;
uint16_t      sampleCount = 0;
uint8_t       avgTemp    = 0;
uint8_t       avgHum     = 0;
uint8_t       lightEventCount = 0;
bool          lightWasAbove   = false;

// ============================================================================
//  SUNRISE / ALARM
// ============================================================================
bool          sunriseActive   = false;
unsigned long sunriseStartMs  = 0;
unsigned long sunriseTotalMs  = 0;

bool          alarmFiring     = false;
unsigned long alarmStartMs    = 0;

const unsigned long STAGE2_MS = 120000UL;
const unsigned long STAGE3_MS = 300000UL;

uint8_t lastAlarmFiredDay = 255;

// ============================================================================
//  SNOOZE
// ============================================================================
bool          snoozeActive  = false;
unsigned long snoozeEndMs   = 0;
uint8_t       snoozeMinutes = 0;

// ============================================================================
//  POST-SLEEP REPORT
// ============================================================================
uint8_t       reportScreen      = 0;
const uint8_t REPORT_SCREENS    = 5;
bool          reportScreenDirty = true;

// ============================================================================
//  SLEEP LOG  (identical struct to original)
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
//  ALARM MELODY  (stage tables identical to original)
// ============================================================================
const uint16_t stage1Notes[][2] = {
  {NOTE_C5, 200}, {NOTE_E5, 200}, {NOTE_G5, 400}
};
const uint8_t  STAGE1_NOTE_COUNT = 3;
const uint16_t STAGE1_GAP_MS     = 10000;

const uint16_t stage2Notes[][2] = {
  {NOTE_C5, 250}, {NOTE_E5, 250}, {NOTE_G5, 500}
};
const uint8_t  STAGE2_NOTE_COUNT = 3;
const uint16_t STAGE2_GAP_MS     = 5000;

const uint16_t stage3Notes[][2] = {
  {NOTE_G5, 100}, {NOTE_A5, 100}, {NOTE_B5, 100}
};
const uint8_t STAGE3_NOTE_COUNT = 3;

struct MelodyState {
  uint8_t       noteIdx;
  uint8_t       stage;
  bool          inGap;
  unsigned long nextTime;
} melody;

// ============================================================================
//  FORWARD DECLARATIONS
// ============================================================================
void initEEPROM();
void loadSettings();
void loadSleepLog();
void saveSettings();
void saveSleepEntry();

void readIR();

void handleIdleState();
void handleSleepState();
void handleSnoozeState();
void handleWakeSequence();
void handleAlarmActive();
void handlePostSleepReport();
void handleSettingsMenu();

void enterSleepMode(DateTime now);
void exitSleepMode(DateTime now);
void checkAlarmTrigger(DateTime now);
void startSunrise(DateTime now);
void startAlarm();
void dismissAlarm(DateTime now);
void triggerSnooze(uint8_t minutes, DateTime now);

void updateClockDisplay(DateTime now);
void updateSleepDisplay(DateTime now);
void showReportScreen(uint8_t screen);
void drawMenuPage();

void updateSunriseLED();
void updateAlarmMelody();
void stopAlarmSounds();
void myTone(uint8_t pin, unsigned int frequency, unsigned long duration);

uint16_t calcSleepMinutes(uint8_t sh, uint8_t sm, uint8_t wh, uint8_t wm);
int16_t  calcSleepDebt();
void     calcConsistency(char *buf);
bool     isAlarmTime(DateTime now);
bool     isSunriseTime(DateTime now);

void sampleDHT();
void sampleLDR();
void bootDiagnostic();

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  pinMode(BUZZER_PIN,      OUTPUT);
  pinMode(RELAY_PIN,       OUTPUT);
  pinMode(SUNRISE_LED_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN,  OUTPUT);
  // Note: no joystick pinMode needed — IR receiver is initialised by IRremote

  digitalWrite(RELAY_PIN,      LOW);
  analogWrite(SUNRISE_LED_PIN, 0);
  analogWrite(STATUS_LED_PIN,  0);

  Serial.begin(9600);
  Wire.begin();

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print(F("DAWNTRACE v3.0"));
  lcd.setCursor(0, 1);
  lcd.print(F("Initializing... "));

  // Start IR receiver
  // DISABLE_LED_FEEDBACK prevents the library from toggling pin 13,
  // which is shared with our LCD D7 data line.
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
  }
  if (!rtc.isrunning()) {
    Serial.println(F("RTC is NOT running, setting time!"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println(F("RTC init"));

  dht.begin();

  initEEPROM();
  loadSettings();
  loadSleepLog();

  analogWrite(STATUS_LED_PIN, 120);
  delay(400);
  analogWrite(STATUS_LED_PIN, 0);

  bootDiagnostic();

  lcd.clear();
  currentState      = STATE_IDLE;
  lastDisplayUpdate = 0;

  Serial.println(F("DAWNTRACE ready — Phase 9 IR build"));
  Serial.println(F("Point remote at receiver and press any button."));
  Serial.println(F("Serial will print raw hex codes for calibration."));
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
  readIR();           // populate evt* flags from IR receiver

  // 1: Restarts the entire program (soft reset)
  if (evtDigit && evtDigitVal == 1) {
    void(* resetFunc) (void) = 0;
    resetFunc();
  }

  DateTime now = rtc.now();

  // Alarm trigger check runs in idle, sleep, and snooze states
  if (currentState == STATE_IDLE      ||
      currentState == STATE_SLEEP_MODE ||
      currentState == STATE_SNOOZE) {
    checkAlarmTrigger(now);
  }

  if (sunriseActive) updateSunriseLED();
  if (alarmFiring)   updateAlarmMelody();

  switch (currentState) {

    // ── IDLE ──────────────────────────────────────────────────────────────
    case STATE_IDLE:
      handleIdleState();
      // 2: Toggle to enter sleep mode
      if (evtDigit && evtDigitVal == 2) {
        enterSleepMode(now);
      }
      // 4: Enter Alarm setting screen
      if (evtDigit && evtDigitVal == 4) {
        menuAlarmHour    = alarmHour;
        menuAlarmMin     = alarmMinute;
        menuSunriseDur   = sunriseDuration;
        menuTargetSleep  = targetSleepHours;
        menuAlarmEnabled = alarmEnabled;
        menuLdrThresh    = ldrThreshold;
        currentMenuPage  = MENU_ALARM_HOUR;
        digitFirst       = 255;
        currentState     = STATE_SETTINGS_MENU;
        lcd.clear();
        drawMenuPage();
      }
      break;

    // ── SLEEP ─────────────────────────────────────────────────────────────
    case STATE_SLEEP_MODE:
      handleSleepState();
      // 2: Toggle to leave sleep mode
      if (evtDigit && evtDigitVal == 2) {
        exitSleepMode(now);
      }
      break;

    // ── SNOOZE ────────────────────────────────────────────────────────────
    case STATE_SNOOZE:
      handleSnoozeState();
      if (evtOK) {
        // OK during snooze → wake up early
        snoozeActive = false;
        startAlarm();
      }
      break;

    // ── WAKE SEQUENCE (sunrise ramp) ──────────────────────────────────────
    case STATE_WAKE_SEQUENCE:
      handleWakeSequence();
      break;

    // ── ALARM ACTIVE ──────────────────────────────────────────────────────
    case STATE_ALARM_ACTIVE:
      handleAlarmActive();
      if (evtOK) {
        dismissAlarm(now);
      }
      if (evtDigit && evtDigitVal == 5) {
        triggerSnooze(SNOOZE_5_MIN, now);
      }
      if (evtDigit && evtDigitVal == 0) {
        triggerSnooze(SNOOZE_10_MIN, now);
      }
      // Any other directional button also dismisses (bedside grab in the dark)
      if (evtUp || evtDown || evtLeft || evtRight || evtStar || evtHash) {
        dismissAlarm(now);
      }
      break;

    // ── POST-SLEEP REPORT ─────────────────────────────────────────────────
    case STATE_POST_SLEEP_REPORT:
      handlePostSleepReport();
      // 3: Press to toggle through pages after sleep mode is exited
      if (evtDigit && evtDigitVal == 3) {
        reportScreen++;
        reportScreenDirty = true;
        if (reportScreen >= REPORT_SCREENS) {
          saveSleepEntry();
          lcd.clear();
          lcd.print(F("  Data saved!   "));
          delay(1200);
          lcd.clear();
          currentState      = STATE_IDLE;
          lastDisplayUpdate = 0;
        }
      }
      break;

    // ── SETTINGS MENU ─────────────────────────────────────────────────────
    case STATE_SETTINGS_MENU:
      handleSettingsMenu();
      break;
  }
}

// ============================================================================
//  IR INPUT
// ============================================================================
void readIR() {
  // Clear all event flags at the start of every loop iteration
  evtUp = evtDown = evtLeft = evtRight = false;
  evtOK = evtStar = evtHash = evtDigit  = false;
  evtDigitVal = 0;

  if (!IrReceiver.decode()) return;

  // IRremote v4: raw data is in decodedIRData.decodedRawData
  uint32_t raw = IrReceiver.decodedIRData.decodedRawData;

  // Resume receiver immediately so we don't miss the next signal
  IrReceiver.resume();

  // Handle NEC repeat codes
  if (raw == IR_REPEAT || IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) {
    // Only propagate repeats for value-change buttons, and rate-limit them
    if ((millis() - lastIRTime) < IR_REPEAT_GAP_MS) return;
    raw = lastIRCode;   // replay the last real button
  } else {
    lastIRCode = raw;
  }
  lastIRTime = millis();

  // In IRremote v4, the raw data formatting changed. If raw doesn't match the old 32-bit codes,
  // we can also fall back to the 8-bit command values parsed natively by the library.
  uint16_t cmd = IrReceiver.decodedIRData.command;

  // Print for calibration — every unique press shows up on Serial Monitor
  Serial.print(F("Protocol: ")); Serial.print(IrReceiver.decodedIRData.protocol);
  Serial.print(F(" | IR raw: 0x")); Serial.print(raw, HEX);
  Serial.print(F(" | cmd: 0x")); Serial.println(cmd, HEX);

  // We check against raw first, and if that fails, we can check known 8-bit commands
  // for standard remotes. For now, we rely on the raw codes since that's what's defined.
  switch (raw) {
    case IR_BTN_UP:    evtUp    = true;              break;
    case IR_BTN_DOWN:  evtDown  = true;              break;
    case IR_BTN_LEFT:  evtLeft  = true;              break;
    case IR_BTN_RIGHT: evtRight = true;              break;
    case IR_BTN_OK:    evtOK    = true;              break;
    case IR_BTN_STAR:  evtStar  = true;              break;
    case IR_BTN_HASH:  evtHash  = true;              break;
    
    // Map number buttons
    case IR_BTN_0: evtDigit = true; evtDigitVal = 0; break;
    case IR_BTN_1: evtDigit = true; evtDigitVal = 1; break;
    case IR_BTN_2: evtDigit = true; evtDigitVal = 2; break;
    case IR_BTN_3: evtDigit = true; evtDigitVal = 3; break;
    case IR_BTN_4: evtDigit = true; evtDigitVal = 4; break;
    case IR_BTN_5: evtDigit = true; evtDigitVal = 5; break;
    case IR_BTN_6: evtDigit = true; evtDigitVal = 6; break;
    case IR_BTN_7: evtDigit = true; evtDigitVal = 7; break;
    case IR_BTN_8: evtDigit = true; evtDigitVal = 8; break;
    case IR_BTN_9: evtDigit = true; evtDigitVal = 9; break;
    default: break;
  }

  // IRremote v4 parses raw data differently (MSB vs LSB). If the 32-bit code didn't match,
  // fall back to the 8-bit command byte which is independent of MSB/LSB formatting!
  if (!evtUp && !evtDown && !evtLeft && !evtRight && !evtOK && !evtStar && !evtHash && !evtDigit) {
    switch (cmd) {
      case 0x62: evtUp    = true;              break;
      case 0x22: evtDown  = true;              break;
      case 0x02: evtLeft  = true;              break;
      case 0xC2: evtRight = true;              break;
      case 0x38: evtOK    = true;              break;
      case 0xA2: evtStar  = true;              break;
      case 0xE2: evtHash  = true;              break;
      
      // Numbers
      case 0x52: evtDigit = true; evtDigitVal = 0; break;
      case 0x68: evtDigit = true; evtDigitVal = 1; break;
      case 0x98: evtDigit = true; evtDigitVal = 2; break;
      case 0xB0: evtDigit = true; evtDigitVal = 3; break;
      case 0x30: evtDigit = true; evtDigitVal = 4; break;
      case 0x18: evtDigit = true; evtDigitVal = 5; break;
      case 0x7A: evtDigit = true; evtDigitVal = 6; break;
      case 0x10: evtDigit = true; evtDigitVal = 7; break;
      case 0x42: evtDigit = true; evtDigitVal = 8; break;
      case 0x4A: evtDigit = true; evtDigitVal = 9; break;
      default: break;
    }
  }
}

// ============================================================================
//  EEPROM  (logic identical to original)
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
  alarmHour        = EEPROM.read(EEPROM_ALARM_HOUR);
  alarmMinute      = EEPROM.read(EEPROM_ALARM_MIN);
  alarmEnabled     = (EEPROM.read(EEPROM_ALARM_ENABLED) == 1);
  sunriseDuration  = EEPROM.read(EEPROM_SUNRISE_DUR);
  targetSleepHours = EEPROM.read(EEPROM_TARGET_SLEEP);
  alarmVolume      = EEPROM.read(EEPROM_ALARM_VOLUME);
  uint8_t ldrH     = EEPROM.read(EEPROM_LDR_THRESH_H);
  uint8_t ldrL     = EEPROM.read(EEPROM_LDR_THRESH_L);
  ldrThreshold     = ((uint16_t)ldrH << 8) | ldrL;

  if (alarmHour    > 23)  alarmHour    = 7;
  if (alarmMinute  > 59)  alarmMinute  = 0;
  if (sunriseDuration < 5  || sunriseDuration  > 60) sunriseDuration  = SUNRISE_DEFAULT_MINUTES;
  if (targetSleepHours < 4 || targetSleepHours > 12) targetSleepHours = SLEEP_DEBT_TARGET_HOURS;
  if (alarmVolume  >  5)  alarmVolume  = 3;
  if (ldrThreshold == 0   || ldrThreshold > 1020)    ldrThreshold     = LDR_THRESHOLD;

  logIndex = EEPROM.read(EEPROM_LOG_INDEX);
  if (logIndex >= 30) logIndex = 0;

  Serial.print(F("Alarm: "));  Serial.print(alarmHour);
  Serial.print(F(":"));        Serial.print(alarmMinute);
  Serial.print(F("  Sunrise:")); Serial.print(sunriseDuration);
  Serial.print(F("min  En:"));  Serial.println(alarmEnabled);
}

void loadSleepLog() {
  for (uint8_t i = 0; i < 30; i++) {
    int addr = EEPROM_LOG_START + (i * 8);
    sleepLog[i].sleepHour   = EEPROM.read(addr + 0);
    sleepLog[i].sleepMin    = EEPROM.read(addr + 1);
    sleepLog[i].wakeHour    = EEPROM.read(addr + 2);
    sleepLog[i].wakeMin     = EEPROM.read(addr + 3);
    sleepLog[i].avgTemp     = EEPROM.read(addr + 4);
    sleepLog[i].avgHum      = EEPROM.read(addr + 5);
    sleepLog[i].lightEvents = EEPROM.read(addr + 6);
    sleepLog[i].dayOfWeek   = EEPROM.read(addr + 7);
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
//  STATE HANDLERS
// ============================================================================

void handleIdleState() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    updateClockDisplay(rtc.now());
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
  if (now - lastDHTSample >= DHT_INTERVAL) { lastDHTSample = now; sampleDHT(); }
  if (now - lastLDRSample >= LDR_INTERVAL) { lastLDRSample = now; sampleLDR(); }
  analogWrite(STATUS_LED_PIN, 0);
}

void handleSnoozeState() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    unsigned long remaining = 0;
    if (snoozeEndMs > millis()) remaining = (snoozeEndMs - millis()) / 1000UL;
    uint8_t remMin = (uint8_t)(remaining / 60);
    uint8_t remSec = (uint8_t)(remaining % 60);
    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "  SNOOZE %dmin   ", snoozeMinutes);
    snprintf(line2, sizeof(line2), "  Back: %02d:%02d   ", remMin, remSec);
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
  }
  // Snooze timer expired → restart alarm
  if (millis() >= snoozeEndMs) {
    snoozeActive = false;
    lcd.clear();
    startAlarm();
  }
}

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
  if (sunriseActive && (millis() - sunriseStartMs) >= sunriseTotalMs) {
    sunriseActive = false;
    analogWrite(SUNRISE_LED_PIN, 255);
    startAlarm();
  }
}

void handleAlarmActive() {
  if (millis() - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = millis();
    DateTime now = rtc.now();
    char line1[17];
    snprintf(line1, sizeof(line1), "  %02d:%02d  ALARM!", now.hour(), now.minute());
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1);
    // Cycle hint messages every 1.5 s
    uint8_t hint = (millis() / 1500) % 3;
    if      (hint == 0) lcd.print(F("OK = dismiss    "));
    else if (hint == 1) lcd.print(F("5  = snooze 5m  "));
    else                lcd.print(F("0  = snooze 10m "));
  }
}

void handlePostSleepReport() {
  if (reportScreenDirty) {
    reportScreenDirty = false;
    showReportScreen(reportScreen);
  }
}

// ============================================================================
//  SETTINGS MENU
// ============================================================================
// Button layout:
//   ◄ / ►  fine adjust  (±1 step, or ±5 min for minute field)
//   ▲ / ▼  coarse adjust (larger jumps — see per-field comment)
//   0–9    direct two-digit entry on hour/minute pages
//   OK     advance to next page
//   #      save & exit immediately from any page
//   *      discard & exit immediately from any page

void handleSettingsMenu() {
  bool redraw = false;

  // ── 9 and 7 to Increase/Decrease ─────────────────────────────────────────
  if (evtDigit && (evtDigitVal == 9 || evtDigitVal == 7)) {
    int dir = (evtDigitVal == 9) ? 1 : -1;
    switch (currentMenuPage) {
      case MENU_ALARM_HOUR:
        menuAlarmHour   = (uint8_t)((menuAlarmHour + dir + 24) % 24); break;
      case MENU_ALARM_MIN:
        menuAlarmMin    = (uint8_t)((menuAlarmMin + dir + 60) % 60); break;
      case MENU_SUNRISE_DUR:
        menuSunriseDur  = (uint8_t)constrain((int)menuSunriseDur  + dir, 5, 60); break;
      case MENU_TARGET_SLEEP:
        menuTargetSleep = (uint8_t)constrain((int)menuTargetSleep + dir, 4, 12); break;
      case MENU_ALARM_TOGGLE:
        menuAlarmEnabled = !menuAlarmEnabled; break;
      case MENU_LDR_THRESH:
        menuLdrThresh   = (uint16_t)constrain((int)menuLdrThresh  + dir * 50, 50, 950); break;
      default: break;
    }
    redraw = true;
  }

  // ── 4 to confirm and move to next screen ──────────────────────────────────
  if (evtDigit && evtDigitVal == 4) {
    if ((uint8_t)currentMenuPage + 1 >= (uint8_t)MENU_PAGE_COUNT) {
      // Last page -> treat as save & exit
      alarmHour        = menuAlarmHour;
      alarmMinute      = menuAlarmMin;
      sunriseDuration  = menuSunriseDur;
      targetSleepHours = menuTargetSleep;
      alarmEnabled     = menuAlarmEnabled;
      ldrThreshold     = menuLdrThresh;
      saveSettings();
      lastAlarmFiredDay = 255;   // let new alarm time take effect tonight

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(F("Settings saved! "));
      lcd.setCursor(0, 1);
      lcd.print(alarmEnabled ? F("Alarm ON  ") : F("Alarm OFF "));
      lcd.print(alarmHour   < 10 ? "0" : ""); lcd.print(alarmHour);
      lcd.print(F(":")); lcd.print(alarmMinute < 10 ? "0" : ""); lcd.print(alarmMinute);
      delay(1500);
      lcd.clear();
      currentState      = STATE_IDLE;
      lastDisplayUpdate = 0;
      return;
    } else {
      currentMenuPage = (MenuPage)((uint8_t)currentMenuPage + 1);
      redraw = true;
    }
  }

  if (redraw) drawMenuPage();
}

void drawMenuPage() {
  lcd.clear();
  switch (currentMenuPage) {

    case MENU_ALARM_HOUR:
      lcd.setCursor(0, 0); lcd.print(F("Alarm Hr [0-9x2]"));
      lcd.setCursor(0, 1);
      lcd.print(F("<"));
      lcd.print(menuAlarmHour < 10 ? "0" : ""); lcd.print(menuAlarmHour);
      lcd.print(F(":"));
      lcd.print(menuAlarmMin  < 10 ? "0" : ""); lcd.print(menuAlarmMin);
      lcd.print(F("> OK=next #=save"));
      break;

    case MENU_ALARM_MIN:
      lcd.setCursor(0, 0); lcd.print(F("Alarm Min[0-9x2]"));
      lcd.setCursor(0, 1);
      lcd.print(F("<"));
      lcd.print(menuAlarmHour < 10 ? "0" : ""); lcd.print(menuAlarmHour);
      lcd.print(F(":"));
      lcd.print(menuAlarmMin  < 10 ? "0" : ""); lcd.print(menuAlarmMin);
      lcd.print(F("> LR=5 UD=15   "));
      break;

    case MENU_SUNRISE_DUR:
      lcd.setCursor(0, 0); lcd.print(F("Sunrise (min)   "));
      lcd.setCursor(0, 1);
      lcd.print(F("<")); lcd.print(menuSunriseDur);
      lcd.print(F("min> LR=5 UD=10 "));
      break;

    case MENU_TARGET_SLEEP:
      lcd.setCursor(0, 0); lcd.print(F("Target Sleep    "));
      lcd.setCursor(0, 1);
      lcd.print(F("<")); lcd.print(menuTargetSleep);
      lcd.print(F("hr>  LR=1 UD=2  "));
      break;

    case MENU_ALARM_TOGGLE:
      lcd.setCursor(0, 0); lcd.print(F("Alarm ON/OFF    "));
      lcd.setCursor(0, 1);
      lcd.print(F("<"));
      lcd.print(menuAlarmEnabled ? F("ON ") : F("OFF"));
      lcd.print(F("> any dir=toggle"));
      break;

    case MENU_LDR_THRESH:
      lcd.setCursor(0, 0); lcd.print(F("Light Threshold "));
      lcd.setCursor(0, 1);
      lcd.print(F("<")); lcd.print(menuLdrThresh);
      lcd.print(F("> LR=50 UD=100  "));
      break;

    case MENU_SAVE_EXIT:
      lcd.setCursor(0, 0); lcd.print(F("  # = SAVE      "));
      lcd.setCursor(0, 1); lcd.print(F("  * = DISCARD   "));
      break;

    default: break;
  }
}

// ============================================================================
//  ALARM TRIGGER LOGIC
// ============================================================================
bool isSunriseTime(DateTime now) {
  if (!alarmEnabled) return false;
  int16_t alarmTotalMin   = (int16_t)alarmHour * 60 + (int16_t)alarmMinute;
  int16_t sunriseStartMin = alarmTotalMin - (int16_t)sunriseDuration;
  int16_t nowMin          = (int16_t)now.hour() * 60 + (int16_t)now.minute();
  if (sunriseStartMin < 0) sunriseStartMin += 1440;
  if (sunriseStartMin < alarmTotalMin) {
    return (nowMin >= sunriseStartMin && nowMin < alarmTotalMin);
  } else {
    return (nowMin >= sunriseStartMin || nowMin < alarmTotalMin);
  }
}

bool isAlarmTime(DateTime now) {
  if (!alarmEnabled) return false;
  return (now.hour() == alarmHour && now.minute() == alarmMinute);
}

void checkAlarmTrigger(DateTime now) {
  if (now.day() == lastAlarmFiredDay) return;

  if (!sunriseActive && !alarmFiring &&
      isSunriseTime(now) &&
      currentState != STATE_ALARM_ACTIVE &&
      currentState != STATE_WAKE_SEQUENCE) {
    startSunrise(now);
  }

  if (!alarmFiring &&
      isAlarmTime(now) &&
      currentState != STATE_ALARM_ACTIVE &&
      currentState != STATE_WAKE_SEQUENCE) {
    analogWrite(SUNRISE_LED_PIN, 255);
    sunriseActive = false;
    startAlarm();
    lastAlarmFiredDay = now.day();
  }
}

void startSunrise(DateTime now) {
  sunriseActive  = true;
  sunriseStartMs = millis();
  sunriseTotalMs = (unsigned long)sunriseDuration * 60UL * 1000UL;
  digitalWrite(RELAY_PIN, LOW);
  currentState   = STATE_WAKE_SEQUENCE;
  lcd.clear();
  Serial.print(F("Sunrise: ")); Serial.print(sunriseDuration); Serial.println(F("min"));
}

void startAlarm() {
  alarmFiring      = true;
  alarmStartMs     = millis();
  melody.stage     = 0;
  melody.noteIdx   = 0;
  melody.inGap     = false;
  melody.nextTime  = 0;
  digitalWrite(RELAY_PIN, HIGH);
  analogWrite(SUNRISE_LED_PIN, 255);
  currentState     = STATE_ALARM_ACTIVE;
  lcd.clear();
  Serial.println(F("ALARM ACTIVE"));
}

void dismissAlarm(DateTime now) {
  alarmFiring      = false;
  sunriseActive    = false;
  snoozeActive     = false;
  lastAlarmFiredDay = now.day();

  stopAlarmSounds();
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  wakeHour   = now.hour();
  wakeMinute = now.minute();

  if (inSleepMode) {
    inSleepMode = false;
    sleepDurationMin = calcSleepMinutes(
        sleepStartHour, sleepStartMinute, wakeHour, wakeMinute);
  } else {
    sleepWasLogged   = false;
    sleepDurationMin = 0;
    sleepStartHour   = 0;
    sleepStartMinute = 0;
  }

  currentState      = STATE_POST_SLEEP_REPORT;
  reportScreen      = 0;
  reportScreenDirty = true;
  lastDisplayUpdate = 0;
  Serial.println(F("Alarm dismissed → report"));
}

void triggerSnooze(uint8_t minutes, DateTime now) {
  alarmFiring   = false;
  snoozeActive  = true;
  snoozeMinutes = minutes;
  snoozeEndMs   = millis() + (unsigned long)minutes * 60000UL;

  stopAlarmSounds();
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  currentState = STATE_SNOOZE;
  lcd.clear();
  char buf[17];
  snprintf(buf, sizeof(buf), "  Snooze: %dmin  ", minutes);
  lcd.setCursor(0, 0); lcd.print(buf);
  lcd.setCursor(0, 1); lcd.print(F("  OK=wake early "));
  Serial.print(F("Snooze ")); Serial.print(minutes); Serial.println(F("min"));
}

// ============================================================================
//  SUNRISE LED — cosine ease-in  (original algorithm)
// ============================================================================
void updateSunriseLED() {
  unsigned long elapsed = millis() - sunriseStartMs;
  if (elapsed > sunriseTotalMs) elapsed = sunriseTotalMs;
  float progress   = (float)elapsed / (float)sunriseTotalMs;
  float brightness = 255.0f * (1.0f - cosf(progress * (float)PI)) / 2.0f;
  uint8_t pwmVal   = (uint8_t)constrain((int)brightness, 0, 255);
  analogWrite(SUNRISE_LED_PIN, pwmVal);
}

// ============================================================================
//  ALARM MELODY — 3 stages  (logic identical to original)
// ============================================================================
void updateAlarmMelody() {
  unsigned long elapsed = millis() - alarmStartMs;
  uint8_t newStage;
  if      (elapsed < STAGE2_MS) newStage = 1;
  else if (elapsed < STAGE3_MS) newStage = 2;
  else                           newStage = 3;

  if (newStage != melody.stage) {
    melody.stage    = newStage;
    melody.noteIdx  = 0;
    melody.inGap    = false;
    melody.nextTime = millis();
    Serial.print(F("Alarm Stage ")); Serial.println(newStage);
  }

  if (millis() < melody.nextTime) return;

  if (melody.stage == 1) {
    if (melody.inGap) { melody.inGap = false; melody.noteIdx = 0; }
    uint16_t freq = stage1Notes[melody.noteIdx][0];
    uint16_t dur  = stage1Notes[melody.noteIdx][1];
    myTone(BUZZER_PIN, freq, dur);
    melody.noteIdx++;
    if (melody.noteIdx >= STAGE1_NOTE_COUNT) {
      melody.inGap = true; melody.noteIdx = 0;
      melody.nextTime = millis() + STAGE1_GAP_MS;
    } else {
      melody.nextTime = millis() + 20;
    }
  }
  else if (melody.stage == 2) {
    if (melody.inGap) { melody.inGap = false; melody.noteIdx = 0; }
    uint16_t freq = stage2Notes[melody.noteIdx][0];
    uint16_t dur  = stage2Notes[melody.noteIdx][1];
    myTone(BUZZER_PIN, freq, dur);
    melody.noteIdx++;
    if (melody.noteIdx >= STAGE2_NOTE_COUNT) {
      melody.inGap = true; melody.noteIdx = 0;
      melody.nextTime = millis() + STAGE2_GAP_MS;
    } else {
      melody.nextTime = millis() + 20;
    }
  }
  else {
    // Stage 3 — continuous rapid loop
    uint16_t freq = stage3Notes[melody.noteIdx][0];
    uint16_t dur  = stage3Notes[melody.noteIdx][1];
    myTone(BUZZER_PIN, freq, dur);
    melody.noteIdx  = (melody.noteIdx + 1) % STAGE3_NOTE_COUNT;
    melody.nextTime = millis() + 10;
  }
}

void stopAlarmSounds() {
  melody.stage    = 0;
  melody.noteIdx  = 0;
  melody.inGap    = false;
  melody.nextTime = 0;
}

void myTone(uint8_t pin, unsigned int frequency, unsigned long duration) {
  if (frequency == 0) {
    delay(duration);
    return;
  }
  unsigned long start = millis();
  unsigned long halfPeriod = 1000000L / frequency / 2;
  pinMode(pin, OUTPUT);
  while (millis() - start < duration) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(pin, LOW);
    delayMicroseconds(halfPeriod);
  }
}

// ============================================================================
//  SLEEP MODE ENTER / EXIT  (identical to original)
// ============================================================================
void enterSleepMode(DateTime now) {
  if (inSleepMode) return;
  inSleepMode        = true;
  sleepWasLogged     = true;
  currentState       = STATE_SLEEP_MODE;
  sleepStartHour     = now.hour();
  sleepStartMinute   = now.minute();
  sleepModeEnteredAt = millis();

  tempSum = humSum = sampleCount = 0;
  avgTemp = avgHum = lightEventCount = 0;
  lightWasAbove = false;

  lastDHTSample = millis() - DHT_INTERVAL;
  lastLDRSample = millis() - LDR_INTERVAL;

  lcd.clear();
  analogWrite(SUNRISE_LED_PIN, 0);
  digitalWrite(RELAY_PIN, LOW);

  Serial.print(F("Sleep ON @ "));
  Serial.print(sleepStartHour); Serial.print(F(":")); Serial.println(sleepStartMinute);
}

void exitSleepMode(DateTime now) {
  if (!inSleepMode) return;
  inSleepMode = false;
  wakeHour    = now.hour();
  wakeMinute  = now.minute();
  sleepDurationMin = calcSleepMinutes(
      sleepStartHour, sleepStartMinute, wakeHour, wakeMinute);
  lcd.clear();
  currentState      = STATE_POST_SLEEP_REPORT;
  reportScreen      = 0;
  reportScreenDirty = true;
  Serial.print(F("Sleep OFF @ "));
  Serial.print(wakeHour); Serial.print(F(":")); Serial.print(wakeMinute);
  Serial.print(F(" — ")); Serial.print(sleepDurationMin / 60);
  Serial.print(F("h ")); Serial.print(sleepDurationMin % 60); Serial.println(F("m"));
}

// ============================================================================
//  ENVIRONMENTAL SAMPLING  (identical to original)
// ============================================================================
void sampleDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) { Serial.println(F("DHT read failed")); return; }
  tempSum += (unsigned long)t;
  humSum  += (unsigned long)h;
  sampleCount++;
  avgTemp = (uint8_t)(tempSum / sampleCount);
  avgHum  = (uint8_t)(humSum  / sampleCount);
  Serial.print(F("DHT T=")); Serial.print(t, 1);
  Serial.print(F(" H=")); Serial.print(h, 1);
  Serial.print(F(" avg(")); Serial.print(sampleCount); Serial.println(F(")"));
}

void sampleLDR() {
  int ldrVal = analogRead(LDR_PIN);
  if ((millis() - sleepModeEnteredAt) < LDR_GRACE_PERIOD) {
    Serial.print(F("LDR=")); Serial.print(ldrVal); Serial.println(F(" (grace)"));
    return;
  }
  bool above = (ldrVal > (int)ldrThreshold);
  if (above && !lightWasAbove) {
    lightEventCount++;
    Serial.print(F("Light event #")); Serial.println(lightEventCount);
  }
  lightWasAbove = above;
}

// ============================================================================
//  DISPLAY FUNCTIONS  (identical to original)
// ============================================================================
void updateClockDisplay(DateTime now) {
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s",
           now.hour(), now.minute(), now.second(), dayNames[now.dayOfTheWeek()]);
  if (alarmEnabled) {
    snprintf(line2, sizeof(line2), "%02d/%02d/%02d A%02d:%02d",
             now.day(), now.month(), now.year() % 100, alarmHour, alarmMinute);
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
        snprintf(line2, sizeof(line2), "Wake: %02d:%02d >   ", wakeHour, wakeMinute);
      } else {
        strcpy(line1,  "Slept: no log   ");
        snprintf(line2, sizeof(line2), "Wake: %02d:%02d >   ", wakeHour, wakeMinute);
      }
      break;
    }
    case 1:
      if (sampleCount > 0) {
        snprintf(line1, sizeof(line1), "Avg Temp: %dC    ", avgTemp);
        snprintf(line2, sizeof(line2), "Avg Hum:  %d%%    ", avgHum);
      } else {
        strcpy(line1, "Avg Temp: --C   ");
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
      int16_t  debt = calcSleepDebt();
      uint16_t dh   = debt / 60;
      uint16_t dm   = debt % 60;
      if (debt == 0) strcpy(line1, "Debt: NONE      ");
      else           snprintf(line1, sizeof(line1), "Debt: %dh %02dmin", dh, dm);
      char pat[12];
      calcConsistency(pat);
      snprintf(line2, sizeof(line2), "Pattern: %-7s", pat);
      break;
    }
    case 4:
      strcpy(line1, "  Log saved!    ");
      strcpy(line2, "  OK or > done  ");
      break;
    default:
      strcpy(line1, "                ");
      strcpy(line2, "                ");
      break;
  }

  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
  Serial.print(F("Report ")); Serial.print(screen + 1);
  Serial.print(F("/")); Serial.print(REPORT_SCREENS);
  Serial.print(F(": ")); Serial.println(line1);
}

// ============================================================================
//  CALCULATIONS  (identical to original)
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
  uint8_t valid    = 0;
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
//  BOOT DIAGNOSTIC  (identical to original, with IR check added)
// ============================================================================
void bootDiagnostic() {
  lcd.clear();
  lcd.print(F("Hardware check.."));
  delay(600);

  lcd.setCursor(0, 1); lcd.print(F("Relay...        "));
  digitalWrite(RELAY_PIN, HIGH); delay(300);
  digitalWrite(RELAY_PIN, LOW);  delay(200);

  lcd.setCursor(0, 1); lcd.print(F("Buzzer...       "));
  myTone(BUZZER_PIN, NOTE_C5, 150); delay(50);
  myTone(BUZZER_PIN, NOTE_G5, 150); delay(50);

  lcd.setCursor(0, 1); lcd.print(F("Sunrise LEDs... "));
  for (int i = 0; i <= 255; i += 3) { analogWrite(SUNRISE_LED_PIN, i); delay(6); }
  for (int i = 255; i >= 0; i -= 3) { analogWrite(SUNRISE_LED_PIN, i); delay(6); }
  analogWrite(SUNRISE_LED_PIN, 0);

  lcd.setCursor(0, 1); lcd.print(F("Status LED...   "));
  analogWrite(STATUS_LED_PIN, 120); delay(400);
  analogWrite(STATUS_LED_PIN, 0);

  lcd.clear();
  lcd.print(F("Sensors...      "));
  delay(800);
  float t   = dht.readTemperature();
  float h   = dht.readHumidity();
  int   ldr = analogRead(LDR_PIN);

  lcd.clear();
  if (isnan(t) || isnan(h)) {
    lcd.print(F("DHT11: ERROR    "));
  } else {
    lcd.print(F("T:")); lcd.print(t, 1);
    lcd.print(F("C H:")); lcd.print(h, 0); lcd.print(F("%"));
  }
  lcd.setCursor(0, 1);
  lcd.print(F("LDR:")); lcd.print(ldr);
  lcd.print(F(" thr:")); lcd.print(ldrThreshold);
  delay(2500);

  lcd.clear();
  lcd.print(F("IR recv on D7   "));
  lcd.setCursor(0, 1);
  lcd.print(F("Press any button"));
  delay(2000);
}