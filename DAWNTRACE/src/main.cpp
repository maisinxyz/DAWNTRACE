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
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
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
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change to 0x3F if your adapter differs
DHT dht(DHT_PIN, DHT_TYPE);

// Keypad setup: 4 rows × 4 columns
const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 4;

char keys[KEYPAD_ROWS][KEYPAD_COLS] = {{'1', '2', '3', 'A'},
                                       {'4', '5', '6', 'B'},
                                       {'7', '8', '9', 'C'},
                                       {'*', '0', '#', 'D'}};

byte rowPins[KEYPAD_ROWS] = {8, 9, 10, 11};   // D8–D11
byte colPins[KEYPAD_COLS] = {12, 13, A1, A3}; // D12, D13, A1, A3

Keypad keypad =
    Keypad(makeKeymap(keys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);

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
bool buttonPressed = false;
bool buttonWasPressed = false;
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
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DAWNTRACE v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // --- RTC init ---
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC NOT FOUND!");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");
    while (1) {
      delay(1000);
    } // halt — RTC is essential
  }

  // Only set time if RTC lost power / time invalid (year < 2020)
  if (!rtc.isrunning()) {
    // Set to compile time on first boot / after battery loss
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println(F("RTC was NOT running — set to compile time"));
  }

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

  // Read keypad
  char key = keypad.getKey();

  switch (currentState) {
  case STATE_IDLE:
    handleIdleState();
    // Check for sleep mode entry via joystick 3s hold
    if (buttonPressed) {
      DateTime now = rtc.now();
      enterSleepMode(now);
    }
    // Keypad [D] also enters sleep mode
    if (key == 'D') {
      DateTime now = rtc.now();
      enterSleepMode(now);
    }
    break;

  case STATE_SLEEP_MODE:
    handleSleepState();
    // Joystick 3s hold exits sleep mode (simulating alarm dismiss for Phase
    // 2-4)
    if (buttonPressed) {
      DateTime now = rtc.now();
      exitSleepMode(now);
    }
    break;

  case STATE_POST_SLEEP_REPORT:
    handlePostSleepReport();
    // Any key advances to next report screen
    if (key != 0) {
      reportScreen++;
      if (reportScreen >= REPORT_SCREENS) {
        // Save log entry and return to idle
        saveSleepEntry();
        lcd.clear();
        currentState = STATE_IDLE;
        lastDisplayUpdate = 0; // force immediate refresh
      } else {
        reportScreenDirty = true;
      }
    }
    // Joystick button also advances
    if (buttonPressed) {
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
  buttonPressed = false; // reset event flag each loop

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
        buttonPressStart = millis();
      } else {
        // Being held — check if 3 seconds elapsed
        if ((millis() - buttonPressStart) >= JOYSTICK_HOLD_MS) {
          buttonPressed = true;     // trigger the event
          buttonWasPressed = false; // reset so it doesn't re-trigger
          buttonPressStart =
              millis(); // prevent re-triggering on continued hold
        }
      }
    } else {
      // Button released
      buttonWasPressed = false;
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
  lcd.noBacklight();
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
  lcd.backlight();
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