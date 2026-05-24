## DAWNTRACE — Product Requirements Document

**Version:** 1.0 **Date:** May 2026 **Hardware:** ELEGOO Uno R3 Most Complete Starter Kit **Dev Machine:** MacBook Air M2 **Target:** Working prototype in 4 weeks

---

### What Is DAWNTRACE?

**Tagline:** Your bedside sleep companion — tracks the night, earns the morning.

**One-liner:** A bedside device that silently logs your sleep schedule and bedroom environment through the night, then wakes you with a gradual sunrise light and rising alarm melody, followed by a post-sleep report of your night.

DAWNTRACE fuses three distinct ideas into one coherent product:

- **From 5D (Sleep Ledger):** A joystick button press logs when you go to sleep and when you wake up. No bright screen, no phone, no app — one press in the dark. EEPROM stores 30 nights of data.
- **From 5A (Ambient Monitor):** A DHT11 sensor tracks temperature and humidity across the night. An LDR watches for light intrusions (streetlights, headlights). All data feeds a morning environmental report.
- **From 5C (Progressive Wake):** At your programmed alarm time, warm LEDs ramp from 0% to 100% brightness along a non-linear curve (slow start, accelerating near wake time). Simultaneously, the passive buzzer builds from a quiet three-note melody to a full alarm sequence.

The result is a single bedside station that **replaces your phone alarm**, **removes your phone from the bed**, and gives you **actual sleep data** — all from a $40 kit and one $2 component.

---

### One Required Purchase

> **⚠️ CRITICAL — Buy This Before You Start**
> 

The ELEGOO kit's LCD1602 comes with a straight pin header for **parallel wiring**, which requires 6 digital pins. Combined with the keypad (8 pins), DHT11, buzzer, relay, LEDs, and joystick, you will run out of pins by a wide margin on the Uno's 14 digital outputs.

The solution is a **PCF8574 I2C LCD backpack adapter** — a small PCB that solders or clips onto the back of the LCD and lets it communicate over the I2C bus (just 2 wires, shared with the RTC). This single $1–3 purchase makes the entire project feasible on a Uno.

**Search:** "PCF8574 I2C LCD 1602 adapter module" on Amazon, AliExpress, or any electronics supplier. Buy 2 in case of a soldering mistake.

---

### Components Used From the ELEGOO Kit

| Component | Qty Used | Role |
| --- | --- | --- |
| UNO R3 board + USB cable | 1 | Microcontroller |
| Breadboard (830 tie-points) | 1 | Circuit prototyping |
| LCD1602 Module | 1 | Display (needs I2C adapter — see above) |
| RTC Module (DS1307) | 1 | Real-time clock + battery backup |
| DHT11 Temp/Humidity Module | 1 | Environmental sensing |
| 4×4 Keypad Module | 1 | Settings input |
| Joystick Module | 1 | Sleep/wake button press |
| Passive Buzzer | 1 | Sunrise alarm melody |
| 5V Relay Module | 1 | External lamp control (optional sunrise) |
| LDR (Photoresistor) | 1 | Ambient light intrusion detection |
| NPN Transistor PN2222 | 1 | PWM control of sunrise LED bank |
| White LEDs | 3 | Sunrise light array |
| Red LED | 1 | Status indicator |
| 220Ω Resistors | 4 | LED current limiting |
| 10kΩ Resistor | 1 | LDR voltage divider |
| 1N4007 Diode | 1 | Relay flyback protection |
| Jumper wires | Many | Connections |

**Components from the kit NOT used in this project:** Stepper motor, servo motor, IR receiver, sound sensor, thermistor, 7-segment displays, tilt switch, active buzzer, RGB LED, potentiometers (used only for tuning), RFID (if included), L293D, 74HC595.

---

### System Architecture

                        `┌─────────────────────────────────┐
                        │         DAWNTRACE SYSTEM         │
                        └──────────────┬──────────────────┘
                                       │
          ┌────────────────────────────┼────────────────────────────┐
          │                            │                            │
    ┌─────▼──────┐            ┌────────▼───────┐          ┌────────▼───────┐
    │   INPUTS   │            │   PROCESSING   │          │   OUTPUTS      │
    └─────┬──────┘            └────────┬───────┘          └────────┬───────┘
          │                            │                            │
   ┌──────┴──────┐            ┌────────┴──────┐            ┌───────┴───────┐
   │ Joystick    │            │ Arduino UNO   │            │ LCD1602       │
   │ (sleep/wake)│            │ (ATmega328P)  │            │ (time/status) │
   ├─────────────┤            │               │            ├───────────────┤
   │ DHT11       │            │  State Machine│            │ Passive Buzzer│
   │ (temp/hum)  │────────────►  EEPROM Log   │────────────► (melody/alarm)│
   ├─────────────┤            │  Sleep Calc   │            ├───────────────┤
   │ LDR         │            │  Sunrise Curve│            │ Sunrise LEDs  │
   │ (light)     │            │               │            │ (via PN2222)  │
   ├─────────────┤            └────────┬──────┘            ├───────────────┤
   │ 4×4 Keypad  │                     │                   │ Relay         │
   │ (settings)  │            ┌────────▼──────┐            │ (ext. lamp)   │
   ├─────────────┤            │ RTC DS1307    │            ├───────────────┤
   │ RTC Module  │            │ (timekeeping) │            │ Red LED       │
   │ (current    │            └───────────────┘            │ (status)      │
   │  time)      │                                         └───────────────┘
   └─────────────┘`

---

### Device State Machine

                      `┌─────────────────┐
        Power On ────►│   IDLE DISPLAY  │◄──────────────────┐
                      │ Time/Temp/Hum   │                   │
                      │ Alarm countdown │                   │
                      └────────┬────────┘                   │
                               │                            │
                    ┌──────────┴──────────┐        ┌───────┴────────┐
                    │  Joystick HOLD 3s   │        │  Any Key Press │
                    │  or Keypad [D]      │        │  (from report) │
                    └──────────┬──────────┘        └───────┬────────┘
                               │                           │
                      ┌────────▼────────┐        ┌─────────▼──────────┐
                      │  SLEEP MODE     │        │  POST-SLEEP REPORT │
                      │ Clock only, dim │        │ Duration/Temp/Light│
                      │ DHT11 sampling  │        │ Debt calculation   │
                      │ LDR monitoring  │        └────────────────────┘
                      └────────┬────────┘                  ▲
                               │                           │
                    ┌──────────┴──────────┐                │
                    │  RTC reaches        │                │
                    │  alarm time         │                │
                    └──────────┬──────────┘                │
                               │                           │
                      ┌────────▼────────┐                  │
                      │  WAKE SEQUENCE  │                  │
                      │ LED ramp 0→100% │                  │
                      │ Gentle melody   │                  │
                      └────────┬────────┘                  │
                               │                           │
                    ┌──────────┴──────────┐                │
                    │  Sunrise complete / │                │
                    │  Full alarm sounds  │                │
                    └──────────┬──────────┘                │
                               │                           │
                      ┌────────▼────────┐                  │
                      │  ALARM ACTIVE   │                  │
                      │ Full brightness  │                  │
                      │ Loud alarm       │──── Joystick ───┘
                      │ Awaiting dismiss │     Button Press
                      └─────────────────┘

                   ┌─────────────────────────────────┐
                   │  SETTINGS MENU (from any state) │
                   │  Keypad [C] to enter             │
                   │  Keypad [B] to exit              │
                   └─────────────────────────────────┘`

---

### Pin Allocation Map

> **Memorize this table. Refer back to it constantly. Every wiring step is based on it.**
> 

`┌─────────────────┬────────────────────────────┬────────────────────────────────┐
│ Arduino Pin     │ Connected To               │ Function                       │
├─────────────────┼────────────────────────────┼────────────────────────────────┤
│ D0              │ *** DO NOT USE ***         │ Serial RX (USB programming)    │
│ D1              │ *** DO NOT USE ***         │ Serial TX (USB programming)    │
│ D2              │ DHT11 DATA pin             │ Temperature/humidity reading   │
│ D3~ (Timer2)   │ Passive Buzzer (+)         │ Alarm melody via tone()        │
│ D4              │ Relay IN pin               │ External lamp on/off           │
│ D5~ (Timer0)   │ PN2222 transistor BASE     │ Sunrise LED PWM brightness     │
│ D6~ (Timer0)   │ Red LED (+) via 220Ω       │ Status indicator               │
│ D7              │ Joystick SW (button)       │ Sleep/wake log press           │
│ D8              │ Keypad ROW 1               │ Keypad scanning                │
│ D9~ (Timer1)   │ Keypad ROW 2               │ Keypad scanning                │
│ D10~ (Timer1)  │ Keypad ROW 3               │ Keypad scanning                │
│ D11~ (Timer2)  │ Keypad ROW 4               │ Keypad scanning                │
│ D12             │ Keypad COL 1               │ Keypad scanning                │
│ D13             │ Keypad COL 2               │ Keypad scanning                │
├─────────────────┼────────────────────────────┼────────────────────────────────┤
│ A0              │ Joystick VRx               │ Menu navigation (X-axis)       │
│ A1              │ Keypad COL 3               │ Keypad scanning (as digital)   │
│ A2              │ LDR (voltage divider)      │ Ambient light monitoring       │
│ A3              │ Keypad COL 4               │ Keypad scanning (as digital)   │
│ A4 (SDA)        │ RTC SDA + LCD SDA          │ I2C data (shared bus)          │
│ A5 (SCL)        │ RTC SCL + LCD SCL          │ I2C clock (shared bus)         │
└─────────────────┴────────────────────────────┴────────────────────────────────┘`

**⚠️ Timer Warning (read before coding):** `tone()` uses Timer2. This disables `analogWrite()` on pins D3 and D11 while a tone is active. In this design: D3 IS the buzzer (driven by `tone()`), and D11 is a keypad row (digital only, never `analogWrite()`). **There is no conflict** — but if you ever change the buzzer pin or add PWM to a Timer2 pin, revisit this.

---

### EEPROM Memory Layout

The ATmega328P has exactly **1,024 bytes** of internal EEPROM. Each byte holds a value 0–255. Here is the layout for DAWNTRACE:

`Address  Size    Contents
─────────────────────────────────────────────────────
0x0000   1 byte  Magic number (0xDA) — confirms EEPROM was initialized
0x0001   1 byte  Alarm hour (0–23)
0x0002   1 byte  Alarm minute (0–59)
0x0003   1 byte  Alarm enabled flag (0 = off, 1 = on)
0x0004   1 byte  Sunrise duration in minutes (5–60)
0x0005   1 byte  Target sleep hours (integer, e.g. 8)
0x0006   1 byte  Alarm volume level (0–5, scaled to PWM duty)
0x0007   3 bytes Reserved for future settings
─────────────────────────────────────────────────────
0x000A   240 bytes  Sleep log: 30 entries × 8 bytes each
         Entry structure (8 bytes per night):
           Byte 0: Sleep-start hour
           Byte 1: Sleep-start minute
           Byte 2: Wake hour
           Byte 3: Wake minute
           Byte 4: Average temperature (°C, integer)
           Byte 5: Average humidity (%, integer)
           Byte 6: Light intrusion event count
           Byte 7: Day of week (0=Sun … 6=Sat)
─────────────────────────────────────────────────────
0x00FA   774 bytes  Unused / reserved
─────────────────────────────────────────────────────`

**30 nights × 8 bytes = 240 bytes. Total used: 250 bytes. Remaining: 774 bytes. Well within 1,024.**

**⚠️ EEPROM write endurance is ~100,000 cycles per address.** If you write a new sleep record every night, a single address will last 273 years. Safe. But never write to EEPROM inside a loop — only write on state change events.

---

### Software Libraries Required

Install all of these through **Arduino IDE → Sketch → Include Library → Manage Libraries** before writing any code:

| Library | Author | Install Name | Purpose |
| --- | --- | --- | --- |
| RTClib | Adafruit | `RTClib` | DS1307 RTC read/write |
| LiquidCrystal I2C | Frank de Brabander | `LiquidCrystal I2C` | LCD with PCF8574 I2C adapter |
| DHT sensor library | Adafruit | `DHT sensor library` | DHT11 temperature/humidity |
| Adafruit Unified Sensor | Adafruit | `Adafruit Unified Sensor` | Required dependency for DHT library |
| Keypad | Mark Stanley, Alexander Brevig | `Keypad` | 4×4 membrane keypad |
| EEPROM | Built-in | (already included) | Persistent storage |
| Wire | Built-in | (already included) | I2C communication |

---

### Build Phase Map

`Phase 0 — Development Environment Setup      (Days 1–2)
Phase 1 — Individual Component Testing       (Days 3–6)
Phase 2 — Core: Clock + Display              (Days 7–8)
Phase 3 — Sleep Logging System               (Days 9–11)
Phase 4 — Environmental Sensing              (Days 12–13)
Phase 5 — Sunrise + Alarm System             (Days 14–17)
Phase 6 — Settings Menu (Keypad UI)          (Days 18–20)
Phase 7 — EEPROM Persistence                 (Days 21–22)
Phase 8 — Full System Integration            (Days 23–25)
Phase 9 — 3D Printed Enclosure              (Days 20–28, parallel)
Phase 10 — Polish + Portfolio                (Days 29–30)`

---

## PHASE 0 — Development Environment Setup

**Goal:** A working, configured MacBook Air M2 development environment where you can upload to the Uno and use all required libraries. **Do not touch hardware until this phase is complete.**

---

### Step 0.1 — Install Arduino IDE

1. Go to `arduino.cc/en/software` and download **Arduino IDE 2.x** (the current version, not the legacy 1.x).
2. Drag the `.app` to your Applications folder.
3. Open it. On first launch, it may prompt to install drivers — allow it.
4. Plug the ELEGOO Uno into your MacBook via USB. The Uno uses a **CH340 or ATmega16U2 USB chip**depending on the revision.

**⚠️ Mac M2 CH340 Driver Issue:** If your Uno is a clone using the CH340 chip (look for a small black chip labeled CH340 near the USB port), macOS Ventura/Sonoma may not recognize it automatically.

- Check: In Arduino IDE → Tools → Port — if you see `/dev/cu.usbmodem...` it works. If the port doesn't appear at all, you need the CH340 driver.
- Fix: Download from `wch-ic.com` or search "CH340 Mac driver." Install it, restart, and retry.
- **The official ELEGOO Uno R3** often uses the genuine ATmega16U2 USB bridge and works without a driver.
1. In the IDE, go to **Tools → Board** and select **"Arduino Uno"**.
2. Go to **Tools → Port** and select the port that appears when the Uno is connected (something like `/dev/cu.usbmodem14201`).

---

### Step 0.2 — Verify Upload Works

1. Go to **File → Examples → 01.Basics → Blink**.
2. Click the Upload button (right arrow).
3. Watch the bottom of the IDE for `Done uploading.`
4. The onboard LED on the Uno (near pin 13) should blink once per second.

**If upload fails:**

- Wrong port selected → recheck Tools → Port
- Wrong board selected → recheck Tools → Board
- USB cable issue → try a different cable (many USB cables are charge-only with no data wires)
- CH340 driver missing (see above)

---

### Step 0.3 — Install All Libraries

1. In Arduino IDE, go to **Sketch → Include Library → Manage Libraries**.
2. In the search bar, install each library from the table in the "Software Libraries Required" section above, one by one.
3. For each one, click **Install**, and if prompted to install dependencies, click **Install All**.

**Good to know:** The Adafruit DHT library requires `Adafruit Unified Sensor` as a dependency. If you install DHT first and then try to compile without the dependency, you will get a confusing error about a missing header file. Always click "Install All" when prompted.

---

### Step 0.4 — Order the I2C LCD Adapter

If you haven't ordered the PCF8574 I2C adapter already, order it now so it arrives before Phase 2. Search: **"PCF8574 IIC I2C serial interface LCD adapter module for 1602"**. Cost ~$1–3.

In the meantime, you can complete Phase 1 steps that don't involve the LCD.

---

## PHASE 1 — Individual Component Testing

**Goal:** Test each component completely in isolation before combining them. This is the single most important discipline in embedded prototyping. If you skip isolation testing and go straight to the full circuit, debugging becomes exponentially harder.

**For each step:** Wire only that component (plus power/ground). Upload an example sketch from the library or the ELEGOO tutorial PDF. Confirm it works. Then remove all wires before the next step.

---

### Step 1.1 — I2C Address Scanner

Before testing the RTC and LCD together, run an I2C scanner to find the addresses of both your RTC module and the PCF8574 LCD adapter. This is crucial because PCF8574 adapters sometimes ship set to `0x27` and sometimes `0x3F`depending on address jumpers.

**Circuit:** Connect RTC module: VCC→5V, GND→GND, SDA→A4, SCL→A5. Also connect the PCF8574 adapter (with LCD attached): VCC→5V, GND→GND, SDA→A4, SCL→A5.

**Sketch:** Go to **File → Examples → Wire → i2c_scanner** (built into Arduino IDE). Upload it, open Serial Monitor (Tools → Serial Monitor, set baud to 9600). It will print the I2C addresses it finds. Write them down:

- DS1307 RTC address: almost always **0x68**
- PCF8574 LCD adapter: **0x27** or **0x3F**

**You will use these addresses in every subsequent LCD and RTC sketch. Record them here.**

---

### Step 1.2 — RTC Module (DS1307)

**What to confirm:** The RTC can be set to the current time and can be read back correctly.

**Circuit:**

`RTC Module      Arduino Uno
─────────────────────────────
VCC          →  5V
GND          →  GND
SDA          →  A4
SCL          →  A5`

**Test:** Use the `ds1307` example from the RTClib library (File → Examples → RTClib → ds1307). First run: it sets the time to the compile time. Open Serial Monitor. You should see the current date and time printing every second.

**Gotchas:**

- The DS1307 requires a **CR2032 coin cell battery** in the holder on the module to keep time when power is off. Check if your ELEGOO kit includes one. If not, buy a CR2032. Without it, the RTC resets to a wrong time on every power cycle.
- If Serial Monitor shows `00:00:00 1/1/2000`, the RTC lost its time — it's running but has no battery backup.
- The RTClib `adjust()` call in the sketch's `setup()` sets the time every time the board boots. **Before your final code**, you must either add a condition (only set if time is invalid) or remove the `adjust()` call — otherwise the clock resets to compile time every time you unplug and replug.

---

### Step 1.3 — LCD1602 with I2C Adapter

**What to confirm:** Text prints on the LCD, backlight works, contrast is visible.

**Before you start:** The PCF8574 I2C adapter must be attached to the LCD. Some come pre-soldered; others require soldering 4 pins. Align the adapter to the LCD's pin header carefully — wrong alignment breaks pins.

**Circuit:**

`PCF8574 Adapter    Arduino Uno
────────────────────────────────
VCC             →  5V
GND             →  GND
SDA             →  A4
SCL             →  A5`

**Test sketch:**

cpp

`#include <LiquidCrystal_I2C.h>
// Use the address you found in Step 1.1 (0x27 or 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);
void setup() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("DAWNTRACE v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Hello, World!");
}
void loop() {}`

**Gotchas:**

- If the LCD shows blocks instead of text, the contrast potentiometer on the PCF8574 adapter needs adjustment. Use a small screwdriver to turn the blue potentiometer on the back of the adapter. Turn it slowly until characters appear.
- If the backlight is off, check your VCC connection. The backlight jumper on the adapter must be in place (it usually is by default).
- If you get a compile error about `LiquidCrystal_I2C`, make sure you installed the library from "Frank de Brabander" (not another version).

---

### Step 1.4 — DHT11 Sensor

**What to confirm:** Temperature and humidity readings appear on Serial Monitor and are in a reasonable range (room temperature, ~30–60% humidity).

**Circuit:**

`DHT11 Module     Arduino Uno
──────────────────────────────
VCC (or +)    →  5V
DATA (middle) →  D2
GND (or -)    →  GND`

**Good to know:** The ELEGOO kit DHT11 is a 3-pin module (with a built-in pull-up resistor on the PCB). If you ever use a raw 4-pin DHT11 sensor instead, you need a 10kΩ pull-up resistor between DATA and VCC.

**Test sketch:** Use File → Examples → DHT sensor library → DHTtester. Change the `DHTPIN` to `2` and `DHTTYPE` to `DHT11`.

**Gotchas:**

- DHT11 has a **sampling rate limit of 1 Hz**. Do not try to call `dht.read()` more than once per second — the sensor will return a stale reading or an error. In your final code, use `millis()` timing to sample every 2 seconds.
- If readings return `nan` (not a number), the sensor isn't wired correctly or you're reading too fast.
- DHT11 accuracy: ±2°C temperature, ±5% humidity. For a sleep tracker, this is perfectly adequate.

---

### Step 1.5 — 4×4 Keypad

**What to confirm:** Every key press is correctly detected and printed to Serial Monitor.

**Circuit:**

`Keypad Pin   Arduino Pin
────────────────────────
Pin 1 (R1)   D8
Pin 2 (R2)   D9
Pin 3 (R3)   D10
Pin 4 (R4)   D11
Pin 5 (C1)   D12
Pin 6 (C2)   D13
Pin 7 (C3)   A1
Pin 8 (C4)   A3`

**⚠️ Verify row/column order by physically looking at your keypad connector.** The ELEGOO keypad has 8 wires in a ribbon cable. The rows are the wires corresponding to the horizontal lines, columns to vertical. Row 1 connects to the top row of buttons. If keys come out wrong, swap the pin assignments.

**⚠️ NEVER connect Keypad pins to D0 or D1.** Those are Serial TX/RX. Using them with the keypad while also having Serial Monitor open will cause garbled output and missed key presses.

**Test sketch:** Use File → Examples → Keypad → HelloKeypad. Update the `rowPins[]` and `colPins[]` arrays to match your wiring above.

**Gotchas:**

- A1 and A3 are analog pins being used as digital pins for the keypad columns. In your `colPins[]` array, use **A1 and A3** as the values (the Keypad library accepts analog pin numbers). On the Uno, A0=14, A1=15, A2=16, A3=17 in digital numbering — you can use either `A1` or `15`.
- Press every key and verify each one registers correctly. If a whole row doesn't register, that row's wire is probably disconnected.

---

### Step 1.6 — Passive Buzzer

**What to confirm:** Different tones play correctly using `tone()`.

**Circuit:**

`Passive Buzzer (+)   →  D3
Passive Buzzer (–)   →  GND`

**Test sketch:** Use File → Examples → 02.Digital → toneMelody. Change the pin to `3`.

**⚠️ Active vs Passive Buzzer:** The ELEGOO kit includes both. The **active buzzer** makes a fixed-frequency beep with just digitalWrite HIGH. The **passive buzzer** requires a PWM frequency signal to make a tone and can play melodies. For DAWNTRACE, use the **passive buzzer**. They look similar — the active buzzer usually has a sticker or hole visible from the top; the passive one often has a smooth green PCB on the bottom. If you're unsure, try both and use whichever produces different pitches with `tone()`.

**Gotchas:**

- `tone()` takes 3 arguments: `tone(pin, frequency, duration_ms)`. If you omit duration, it plays indefinitely until `noTone(pin)` is called.
- The passive buzzer is quiet without amplification. That's fine for this project — the alarm volume is configurable, and in a quiet bedroom it will be audible.

---

### Step 1.7 — Sunrise LED Bank + PN2222 Transistor

**What to confirm:** Three white LEDs smoothly dim from 0% to 100% brightness under PWM control.

**Why a transistor?** Arduino digital pins can source a maximum of **20mA safely** (40mA absolute max). Three white LEDs each draw ~20mA = 60mA total. This exceeds the pin's safe current and will damage the microcontroller over time. The PN2222 transistor acts as a switch: the Arduino pin drives a tiny base current, and the transistor handles the full LED current from the 5V rail.

**Circuit:**

`D5~ ──── 1kΩ resistor ──── PN2222 BASE (center pin)
5V  ──── 220Ω ──── White LED 1 (+) ──┐
5V  ──── 220Ω ──── White LED 2 (+) ──┼──── PN2222 COLLECTOR (right pin, flat side forward)
5V  ──── 220Ω ──── White LED 3 (+) ──┘
PN2222 EMITTER (left pin) ──── GND`

**PN2222 pin identification (flat side facing you):**

    `Flat side
   ┌─────────┐
   │  PN2222 │
   └──┬──┬──┘
      │  │  └── Collector (C) — right
      │  └──── Base (B) — center
      └──────── Emitter (E) — left`

**Test sketch:** Use `analogWrite(5, value)` in a loop that increases value from 0 to 255 and back, with small delays. You should see the LEDs breathe smoothly.

**Gotchas:**

- The 1kΩ resistor on the base is important. Without it, too much base current flows and can damage the transistor or the Arduino pin.
- The 220Ω resistors on each LED limit current to ~(5V – 2.1V forward drop) / 220Ω ≈ 13mA per LED. Safe.
- If LEDs are either fully on or fully off (no dimming), check that D5 is being used and not a non-PWM pin.
- **PWM only dims correctly on pins marked `~`**: D3, D5, D6, D9, D10, D11.

---

### Step 1.8 — LDR (Photoresistor)

**What to confirm:** The analog reading changes when you cover the LDR with your hand vs expose it to light.

**Circuit — voltage divider:**

`5V ──── LDR ──── A2 ──── 10kΩ ──── GND`

The LDR resistance decreases in bright light and increases in darkness. With the 10kΩ pull-down resistor, `analogRead(A2)` returns a high value (~900+) in bright light and a low value (<200) in darkness.

**Test sketch:** Read `analogRead(A2)` in a loop and print to Serial Monitor. Cover/uncover the LDR and watch the values change.

**Gotchas:**

- In your final code, calibrate the "light intrusion" threshold for your specific bedroom. 2am in a dark room might give readings of 50–100. A passing car's headlights might spike to 400+. You'll set this threshold in the settings menu.
- The LDR only needs to be sampled every few seconds during sleep — no need for high-speed sampling.

---

### Step 1.9 — Joystick Module (Button Only)

**What to confirm:** The joystick button press is reliably detected as a digital LOW (it pulls to ground when pressed).

**Circuit:**

`Joystick VCC  →  5V
Joystick GND  →  GND
Joystick SW   →  D7
Joystick VRx  →  A0`

(VRy left unconnected for this project — you only use X-axis and button)

**Test sketch:** `pinMode(7, INPUT_PULLUP)` then read `digitalRead(7)`. When you press the joystick button, it reads LOW. When released, it reads HIGH (internal pull-up keeps it HIGH).

**Gotchas:**

- The joystick button uses **active-low** logic: pressed = LOW, not pressed = HIGH. Use `INPUT_PULLUP` in `pinMode()`— this enables Arduino's internal 20kΩ pull-up resistor, so you don't need an external resistor.
- For the 3-second hold detection (sleep/wake toggle), use `millis()` to track how long the button has been held. Do NOT use `delay()` for this — it freezes the entire program.

---

### Step 1.10 — 5V Relay Module

**What to confirm:** The relay clicks on and off under Arduino control. The onboard LED shows relay state.

**Circuit:**

`Relay IN    →  D4
Relay VCC   →  5V
Relay GND   →  GND`

**⚠️ Safety Warning:** The relay's output contacts (COM, NO, NC) can switch AC mains voltage (wall outlet). **For all prototype testing, only connect a low-voltage DC lamp or a small 5V LED to the relay output.** Never connect mains AC to the relay while it's on your breadboard. The relay can be used to switch an AC desk lamp, but do this only when the finished device is in a proper enclosure.

**The 1N4007 flyback diode:** Relay coils are inductors. When the relay is switched off, they produce a voltage spike (back-EMF) that can damage the Arduino pin. To protect against this:

`1N4007 diode across relay coil: stripe (cathode) toward VCC, other end toward GND.
This is already on the ELEGOO relay module — it has the diode built in.`

**Test sketch:** `digitalWrite(4, HIGH)` to energize relay (click on), `digitalWrite(4, LOW)` to release (click off). The relay module LED should mirror state.

**Gotchas:**

- Some relay modules are **active-LOW** (LOW = energized, HIGH = off). Test yours: if relay clicks when you send LOW and releases on HIGH, it's active-low. Adjust your code accordingly.
- In the final design, the relay turns on an external desk lamp or LED strip at full brightness as the alarm escalates.

---

## PHASE 2 — Core Integration: Clock + Display

**Goal:** Combine the RTC and LCD to create the device's main clock face — the foundation everything else builds on.

---

### Step 2.1 — Wire RTC and LCD Together on Breadboard

Both the RTC (DS1307) and the LCD adapter (PCF8574) share the I2C bus. On the Arduino Uno, the I2C bus is pins **A4 (SDA)** and **A5 (SCL)**. You connect both devices to the same two wires.

`I2C Bus Diagram:

A4 (SDA) ────┬──────────────┐
             │              │
         RTC SDA       LCD SDA (on PCF8574)

A5 (SCL) ────┬──────────────┐
             │              │
         RTC SCL       LCD SCL (on PCF8574)

5V ──────────┼──────────────┤
             │              │
         RTC VCC       LCD VCC

GND ─────────┼──────────────┤
             │              │
         RTC GND       LCD GND`

**I2C addressing:** Each device has a unique 7-bit I2C address (found in Step 1.1). The Wire library uses these addresses to communicate with the right device. DS1307 = 0x68, PCF8574 LCD = 0x27 or 0x3F. They will not interfere with each other on the same bus.

---

### Step 2.2 — Create the Main Clock Display Sketch

This is your first multi-component sketch. It should:

1. Read the current time from RTC every second using `millis()` (not `delay(1000)`)
2. Display on line 1: `HH:MM:SS` (24-hour format) and the day abbreviation
3. Display on line 2: date `DD/MM/YY`

**Good to know:** Use `millis()` for all timing in this project. `delay()` pauses the entire program — every use of `delay()`in your final code is a place where buttons can't be read, sensors can't sample, and the display can't update. The `millis()`pattern:

cpp

`unsigned long lastUpdate = 0;
void loop() {
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    // do the timed thing here
  }
  // rest of loop continues running
}`

---

### Step 2.3 — RTC Time-Setting Logic

**The RTC must only be set once.** Here is the correct pattern:

1. In `setup()`, read the RTC. If the time returned is invalid (before 2020, or power was lost without a battery), call `rtc.adjust()` with a hardcoded or compile-time value.
2. Once the RTC has a battery and has been set, it keeps time without power indefinitely.
3. Add a settings menu option (Phase 6) to manually set the time from the keypad.

**Struggle point:** Students often set the time every boot by always calling `rtc.adjust()` in `setup()`. The result is the clock always starts at the compile time when you plug in. Fix: check if time is valid first.

---

### Step 2.4 — Verify Display Modes

By the end of Phase 2, pressing a key should not exist yet, but manually test by hardcoding:

- Main display: time, date, temperature placeholder ("-- °C")
- Confirm the display refreshes every second without flicker
- Confirm the LCD backlight is fully off when you call `lcd.noBacklight()` — you'll use this in sleep mode

## PHASE 3 — Sleep Logging System

**Goal:** Add the joystick button to toggle sleep mode, record timestamps, calculate sleep duration, and compute sleep debt.

---

### Step 3.1 — Add Joystick to the Circuit

Add joystick button (D7) and VRx (A0) to the breadboard. Keep the RTC + LCD circuit from Phase 2.

---

### Step 3.2 — Implement 3-Second Hold Detection

The user holds the joystick button for 3 seconds to enter sleep mode (preventing accidental presses in the dark). Here is the logic pattern to implement:

`FLOW:
button not held → do nothing
button just pressed → record pressStartTime = millis()
button held → if (millis() - pressStartTime > 3000) → trigger sleep toggle
button released before 3s → cancel (treat as accidental press or short-press for display)`

**Why 3 seconds?** Short enough to not be annoying, long enough to prevent accidental activation when reaching for something in the dark.

---

### Step 3.3 — Sleep/Wake State Toggle

When sleep mode is triggered:

1. Record `sleepStartHour` and `sleepStartMinute` from RTC into variables
2. Set `inSleepMode = true`
3. Call `lcd.noBacklight()` — turn off the LCD backlight completely
4. Set `digitalRead(6, LOW)` — turn off status LED or set it dim red

When wake mode is triggered (alarm completes and joystick pressed to dismiss):

1. Record `wakeHour` and `wakeMinute` from RTC
2. Calculate sleep duration in minutes: `(wakeHour*60 + wakeMinute) - (sleepHour*60 + sleepMin)`
3. Handle midnight crossover: if result is negative, add 1440 (minutes in a day)
4. Set `inSleepMode = false`
5. Restore display

---

### Step 3.4 — Sleep Debt Calculation

Sleep debt is the running difference between your target sleep and your actual sleep, accumulated across the past week.

**Algorithm:**

`For each of the last 7 nights stored in EEPROM:
  debt += (targetSleepMinutes - actualSleepMinutes)
  if debt < 0: debt = 0  // can't have negative debt (recovered sleep)

Display: debt in hours and minutes`

**Display string example:** `Debt: 2h 20min` or `DEBT: NONE` if caught up.

---

### Step 3.5 — Weekly Consistency Score

Bedtime consistency is calculated as the spread (range, not standard deviation — simpler on Arduino) of sleep start times across the past 7 nights.

`max_sleep_start = maximum of last 7 sleep start minutes-since-midnight
min_sleep_start = minimum of last 7 sleep start minutes-since-midnight
consistency_spread = max_sleep_start - min_sleep_start

if spread < 30:   display "CONSISTENT"
if spread < 60:   display "MODERATE"
else:             display "IRREGULAR"`

**Struggle point:** Midnight crossover. If you go to sleep at 11:30pm one night (23*60+30 = 1410 min) and at 12:30am another (30 min), the spread calculation gives 1380 — wrong. Fix: if any value is under 120 (before 2am), add 1440 to it before comparing. This wraps the clock correctly.

---

## PHASE 4 — Environmental Sensing

**Goal:** Add DHT11 and LDR. Sample during sleep mode. Compute averages. Include in morning report.

---

### Step 4.1 — Add DHT11 and LDR to Circuit

Add DHT11 (D2) and LDR voltage divider (A2 + 10kΩ to GND) to the existing circuit.

---

### Step 4.2 — Continuous Sampling During Sleep Mode

While `inSleepMode = true`, sample DHT11 every 2 minutes and LDR every 30 seconds.

**Running average for temperature and humidity:**

`tempSum += newTemp;
humSum += newHum;
sampleCount++;
avgTemp = tempSum / sampleCount;
avgHum = humSum / sampleCount;`

Use `unsigned int` for the sums if you expect many samples, since `int` (max 32,767) could overflow with many readings accumulated over many hours.

**Light intrusion events:** Increment a counter whenever LDR reads above your threshold AND the device has been in sleep mode for at least 5 minutes (prevents counting the room light before you fall asleep).

---

### Step 4.3 — Post-Sleep Morning Report

After the alarm is dismissed, the LCD cycles through these screens (user presses any key to advance):

`Screen 1: "Slept: 7h 22min" / "Wake: 06:30"
Screen 2: "Avg Temp: 19.0C" / "Avg Hum:  52%"
Screen 3: "Light Events: 3" / "LDR Peak: Hi"
Screen 4: "Debt: 0h 38min" / "Pattern: MOD"
Screen 5: "Log saved!" / "Press any key"`

Screen 5 saves the record to EEPROM (Phase 7).

**Display tip:** `lcd.print()` only adds characters — it doesn't clear old ones. Always use `lcd.clear()` or overwrite with spaces when changing a screen, or you'll get ghost characters.

---

## PHASE 5 — Sunrise + Alarm System

**Goal:** Implement the progressive wake sequence: LED brightness ramp, escalating melody, relay lamp control.

---

### Step 5.1 — Add Buzzer, LED Bank, and Relay to Circuit

Add passive buzzer (D3), sunrise LED bank via PN2222 (D5), red status LED (D6), and relay (D4).

**Full circuit at this point:**

`Arduino Uno
├── D2  → DHT11
├── D3  → Passive Buzzer (+)  [GND → GND]
├── D4  → Relay IN
├── D5  → 1kΩ → PN2222 Base
│          PN2222 Collector → White LED 1/2/3 (each with 220Ω to 5V)
│          PN2222 Emitter → GND
├── D6  → 220Ω → Red LED → GND
├── D7  → Joystick SW (SW to GND through INPUT_PULLUP)
├── D8-D11 → Keypad Rows 1-4
├── D12-D13, A1, A3 → Keypad Cols 1-4
├── A0  → Joystick VRx
├── A2  → LDR (with 10kΩ to GND)
├── A4  → RTC SDA + LCD SDA
└── A5  → RTC SCL + LCD SCL`

---

### Step 5.2 — Non-Linear Brightness Ramp

The sunrise simulation uses a **cosine-based ease-in curve** rather than a linear ramp. Linear feels mechanical. The cosine curve is slow at the start, accelerating near wake time — mimicking how sunlight actually builds.

**The math:**

`progress = elapsed_ms / total_sunrise_ms  // 0.0 to 1.0
brightness = 255 * (1 - cos(progress * PI)) / 2

// At progress = 0.0: brightness = 0
// At progress = 0.5: brightness = 127 (halfway)
// At progress = 1.0: brightness = 255`

On Arduino, `cos()` works in radians and requires `<math.h>` (already included by default). Use `float` for the calculation.

**Update rate:** Recalculate and write brightness with `analogWrite(5, brightness)` every 2 seconds. Over a 20-minute sunrise (1200 seconds / 2 = 600 updates), this is smooth enough visually.

`BRIGHTNESS CURVE (cosine ease-in vs linear):

100% ┤                                   ████
     │                               ████
 75% ┤                           ████
     │                       ████
 50% ┤                   ████
     │              ████
 25% ┤          ████
     │       ██
  5% ┤   ████
  0% ┼───┼──────────────────────────────────
     0   5   10   15   20 minutes (example)

Cosine curve: slow start, rapid finish near wake time
Linear would be a diagonal straight line — avoid`

---

### Step 5.3 — Progressive Alarm Melody

The alarm proceeds in three stages:

**Stage 1 (T-0 to T+2 min):** 3-note ascending gentle tone, quiet volume. Repeat every 10 seconds.

`Tone sequence: C5 (523Hz, 200ms) → E5 (659Hz, 200ms) → G5 (784Hz, 400ms) → silence (10s)`

**Stage 2 (T+2 to T+5 min):** Same tones, but playing every 5 seconds. Slightly longer durations.

`Tone sequence: same notes, 250ms each, repeat every 5s`

**Stage 3 (T+5 min and beyond, until dismissed):** Urgent alarm pattern. Plays continuously.

`Tone sequence: G5 (784Hz, 100ms) → A5 (880Hz, 100ms) → B5 (988Hz, 100ms) repeat at ~2Hz`

**Implementation pattern using millis():**

`if alarm active:
  current_time = millis()
  if (current_time - alarmStartTime) < 2 minutes:
    stage = 1
  elif (current_time - alarmStartTime) < 5 minutes:
    stage = 2
  else:
    stage = 3
  play_stage(stage)`

**Gotchas:**

- `tone()` is non-blocking for the specified duration. After the duration, the pin goes silent.
- Do not call `tone()` every loop iteration — the repeated calls reset the tone and cause stuttering. Use a millis-based pattern to call it only when the next note is due.
- You CANNOT use `delay()` between notes — the program must remain responsive to button presses. Use millis timing for all note spacing.

---

### Step 5.4 — Relay for External Lamp

At alarm time (the exact programmed wake time, not the sunrise start), activate the relay:

cpp

`digitalWrite(4, HIGH);  // or LOW if your relay is active-low`

This energizes the relay and turns on whatever lamp is connected. At the end of the morning report (user dismisses), turn the relay off OR leave it on — configurable setting.

**Hardware note:** The lamp connects to the relay COM and NO (normally open) terminals. When the relay energizes, COM and NO connect, completing the lamp circuit.

---

### Step 5.5 — Alarm Without Sleep Mode

Design for the case where the user didn't press the joystick to log sleep but still has an alarm set. The alarm should fire at the programmed time regardless. In this case, the morning report shows only wake time (no sleep start, no duration — display "Sleep not logged").

---

## PHASE 6 — Settings Menu (Keypad UI)

**Goal:** A full keypad-driven settings menu the user can navigate to set alarm time, sunrise duration, target sleep hours, and toggle the alarm.

---

### Step 6.1 — Add Keypad to Circuit

Add the 4×4 keypad (D8–D11 rows, D12–D13–A1–A3 cols) to the circuit.

---

### Step 6.2 — Menu Structure

`[MAIN DISPLAY]
     │
     ▼ Press [C]
[SETTINGS MENU]
     │
     ├── [1] Set Alarm Time      → Enter HH:MM via keypad
     ├── [2] Sunrise Duration    → Select 10/15/20/30 min with [*]/[#]
     ├── [3] Target Sleep Hours  → Enter hours (6–9) with number keys
     ├── [4] Alarm ON/OFF        → Toggle with [A]
     ├── [5] Light Threshold     → Adjust LDR sensitivity with [*]/[#]
     ├── [6] Set Clock           → Enter current time manually
     └── [B] Back to Main Display`

---

### Step 6.3 — Number Entry Pattern (Time Setting)

Entering alarm time HH:MM via keypad is a common UI challenge on small displays. The recommended approach:

1. Display `Alarm: __:__` (underscores as cursor placeholders)
2. User presses number keys one at a time: 0, 6, 3, 0 → builds "06:30"
3. Use a 4-element character array: `char timeInput[4]` and an index `inputPos = 0`
4. Each valid number key appended to array, cursor advances
5. After 4 digits entered: display `Alarm: 06:30` → press [A] to confirm or [B] to cancel
6. Validate: hour 0–23, minute 0–59. If invalid, flash "INVALID" on LCD and restart entry

**Keypad key assignments:**

`Key  Action
──────────────────────────────────────────
0–9  Number input
A    Confirm/Select
B    Back/Cancel
C    Enter settings (from main display)
D    Toggle sleep mode manually
*    Decrease value / previous option
#    Increase value / next option`

---

### Step 6.4 — Display During Menu

- Menu screen shows: `[1] Alarm Time` on line 1, `[B] Back` on line 2
- Arrow navigation: joystick X-axis (A0) can scroll through options — if A0 < 300, go up; if A0 > 700, go down (thresholds vary, calibrate)
- Alternatively: just use number keys 1–6 to jump directly to each setting

**Gotchas:**

- Always call `lcd.clear()` when transitioning between menu screens to avoid character ghosting
- The Keypad library's `getKey()` returns a character (`char`), not an integer. `'1'` is not the same as `1`. To get the numeric value from a keypad key character: `int val = key - '0';`

---

## PHASE 7 — EEPROM Persistence

**Goal:** All settings and sleep log survive power-off. Device boots into correct state.

---

### Step 7.1 — Initialize EEPROM on First Boot

Write a **magic number** (0xDA = 218) to address 0x0000 on first use. On boot, read address 0x0000:

- If it equals 0xDA → EEPROM has been initialized → load saved settings
- If it equals anything else (factory value is 0xFF = 255) → first boot → write default settings to EEPROM

`BOOT SEQUENCE:
┌───────────────────────────────────┐
│ Read EEPROM[0]                    │
└────────────────┬──────────────────┘
                 │
         ┌───────▼──────────┐
         │ == 0xDA?         │
         └──┬────────────┬──┘
            │ YES        │ NO
            ▼            ▼
     Load settings   Write defaults
     from EEPROM     to EEPROM
            │            │
            └────┬───────┘
                 ▼
          Normal operation`

---

### Step 7.2 — Save Settings on Change

Every time a setting changes (user confirms in menu), write it to the corresponding EEPROM address immediately. Use `EEPROM.update()` instead of `EEPROM.write()`:

`EEPROM.update(address, value);
// update() checks if the value is different before writing
// This dramatically extends EEPROM life by avoiding unnecessary writes`

**Do not** save settings on every loop iteration. Save only when the user explicitly confirms a change.

---

### Step 7.3 — Save Sleep Log Entry

At the end of the morning report (Step 4.3, Screen 5), save the night's entry to EEPROM:

**Log rotation:** The 30 entries form a circular buffer. Keep a "next write index" (0–29) saved in EEPROM. After each save, increment it (modulo 30). The oldest entry is always overwritten by the newest.

`logIndex = EEPROM.read(NEXT_LOG_INDEX_ADDR);  // address outside the log area
int baseAddr = LOG_START_ADDR + (logIndex * 8);
EEPROM.update(baseAddr + 0, sleepHour);
EEPROM.update(baseAddr + 1, sleepMinute);
...
logIndex = (logIndex + 1) % 30;
EEPROM.update(NEXT_LOG_INDEX_ADDR, logIndex);`

---

### Step 7.4 — Load Data on Boot

`loadSettings():
  alarmHour     = EEPROM.read(0x01)
  alarmMinute   = EEPROM.read(0x02)
  alarmEnabled  = EEPROM.read(0x03)
  sunriseDuration = EEPROM.read(0x04)
  targetSleep   = EEPROM.read(0x05)

loadSleepLog():
  for i in 0..29:
    read 8 bytes from LOG_START + i*8 into log[i]`

**Gotchas:**

- `EEPROM.read()` returns 255 (0xFF) for unwritten addresses. Validate loaded values — if `alarmHour` = 255, that's invalid. Use defaults instead.
- After extensive testing, you may find you want to wipe and re-initialize EEPROM. Write a simple utility sketch that writes 0xFF to all 1024 bytes. This resets the magic number and triggers fresh initialization on next boot.

---

## PHASE 8 — Full System Integration and Testing

**Goal:** All components running simultaneously in a unified state machine. Verify reliability over 24+ hours.

---

### Step 8.1 — State Machine Implementation

Implement the device states as a `switch` statement on an `enum`:

cpp

`enum DeviceState {
  STATE_IDLE,
  STATE_SLEEP_MODE,
  STATE_WAKE_SEQUENCE,
  STATE_ALARM_ACTIVE,
  STATE_POST_SLEEP_REPORT,
  STATE_SETTINGS_MENU
};

DeviceState currentState = STATE_IDLE;

void loop() {
  switch (currentState) {
    case STATE_IDLE:
      handleIdleState();
      break;
    case STATE_SLEEP_MODE:
      handleSleepState();
      break;
    // ... etc
  }
}`

Each `handleXxxState()` function reads inputs relevant to that state and decides whether to transition.

---

### Step 8.2 — Integration Test Checklist

Run through this checklist in order. Do not skip items:

- [ ]  Power on → RTC loads correct time, settings load from EEPROM
- [ ]  Hold joystick 3s → enters sleep mode, LCD backlight off
- [ ]  In sleep mode → DHT11 samples every 2 minutes (verify via Serial Monitor)
- [ ]  In sleep mode → LDR monitors light (flash torch at sensor, verify counter increments)
- [ ]  Set alarm to 2 minutes from now → verify sunrise LEDs begin ramping at correct time
- [ ]  Sunrise ramp → LEDs complete 0→100% over correct duration with visible cosine curve shape
- [ ]  At alarm time → buzzer starts Stage 1 melody
- [ ]  After 2 minutes → buzzer transitions to Stage 2 (more frequent)
- [ ]  After 5 minutes → buzzer transitions to Stage 3 (urgent)
- [ ]  At alarm time → relay activates (click heard)
- [ ]  Joystick press → alarm dismisses, report displays
- [ ]  Cycle through all 5 report screens
- [ ]  At report Screen 5 → data saves to EEPROM
- [ ]  Power off, power on → settings still present, log entry still present
- [ ]  Enter settings menu → set new alarm time → save → verify in EEPROM
- [ ]  Test with alarm DISABLED → verify no sunrise or buzzer fires
- [ ]  Sleep debt calculation → check math with known entries

---

### Step 8.3 — Common Integration Bugs

**Issue: LCD shows garbled text or old characters persist** Fix: Always call `lcd.clear()` before printing to a new screen. Or: print spaces to overwrite characters you want blank.

**Issue: DHT11 readings stop updating after a few hours** Fix: This is a sampling rate issue. Ensure you're not calling `dht.read()` more than once per second. Use a millis-based 2-second timer for sampling.

**Issue: Alarm fires at wrong time** Fix: Verify your RTC is set correctly. Also verify AM/PM confusion — use 24-hour time exclusively throughout your code.

**Issue: Joystick button registers multiple presses (bouncing)** Fix: Add software debouncing. After detecting a state change, ignore further changes for 50ms:

cpp

`if (buttonState != lastButtonState && millis() - lastDebounceTime > 50) {
  lastDebounceTime = millis();
  // process button event
}`

**Issue: Sunrise LED brightness doesn't look smooth** Fix: Check you are writing to D5 (PWM-capable) and using `analogWrite(5, value)`, not `digitalWrite(5, HIGH/LOW)`. Also ensure the PN2222 transistor base is getting proper drive (1kΩ resistor is correct).

**Issue: `millis()` overflow** `millis()` returns an `unsigned long` and overflows after ~49 days. Always use subtraction for timing: `millis() - lastTime`. Never compare with `>` directly to an absolute millisecond target. The subtraction handles overflow correctly.

---

### Step 8.4 — Reliability Soak Test

Let the device run for 8+ hours overnight with the alarm set. Check the next morning:

- Did the alarm fire at the right time?
- Is the morning report accurate?
- Did EEPROM save correctly?
- Any Serial Monitor error messages? (Leave your laptop plugged in with Serial Monitor open)

---

## PHASE 9 — 3D Printed Enclosure

**Goal:** A polished physical housing that transforms the breadboard prototype into something that looks and feels like a real product. Start the design in parallel with Phase 5–6 (so you have time to iterate prints).

---

### Step 9.1 — Choose Your Design Software

**Recommended for beginners:** **TinkerCAD** (free, browser-based, no installation). Excellent for box-style enclosures with cutouts.

**Recommended for portfolio quality:** **Fusion 360** (free for personal/educational use, download required). Produces more refined, parametric shapes. Worth learning if you have time.

**Printer settings to verify before designing:**

- What is your printer's bed size? (Common: 220×220mm for Ender 3 style)
- What is your layer height? (0.2mm standard, 0.15mm for fine details)
- What filament? (PLA recommended — easy to print, good for indoor devices)

---

### Step 9.2 — What to Design and Print

Design the enclosure as multiple pieces that assemble:

**Part 1: Main Body (print first)**

`┌──────────────────────────────────┐
│  [LCD CUTOUT]    [LED DOME]      │  ← Top face
│                                  │
│  [KEYPAD CUTOUT]                 │  ← Front face (angled 15°)
│                                  │
│  [USB PORT]  [RELAY CABLE EXIT]  │  ← Rear face
└──────────────────────────────────┘
  Dimensions: approx 120mm × 80mm × 60mm`

- **LCD window:** exact 75×27mm cutout on the top face, with a 2mm bezel lip to hold the LCD in place from below
- **Keypad opening:** sized for your specific keypad's active area. Measure your keypad precisely — these vary.
- **USB port cutout:** on the rear, for power cable access. Size: ~12mm × 5mm
- **Relay cable exit:** a 10mm round hole or slot, with a printed strain-relief clip inside
- **DHT11 vent:** 3–4 small 3mm holes on the side where the DHT11 sits, allowing airflow to the sensor (if enclosed too tightly, the sensor reads the device's self-heating, not room temperature)

**Part 2: LED Diffuser Dome** A small semi-spherical or cylindrical dome that sits over the sunrise LED bank. Printed in **translucent/natural PLA** (not white, not colored — you want light transmission). The dome diffuses the three point-source LEDs into a single soft glow. Height: 20–25mm above LED level, diameter: 30–35mm.

**Part 3: Joystick Button Cap** The joystick's raw thumbstick is small and hard to find in the dark. Design a large (40–50mm diameter) smooth dome button cap that press-fits over the joystick thumbstick. This becomes the "sleep/wake button" — it should feel substantial and satisfying.

**Part 4: Bottom Plate with Mounting Posts** A flat bottom that clips or screws to the main body, with printed standoff posts to hold the Arduino Uno at the right height. Include rubber-foot mounting holes (4 corners) — stick-on rubber feet ($1 from hardware store) are a professional finishing touch.

---

### Step 9.3 — Design Workflow

1. **Measure everything first.** Before opening design software, measure with calipers or a ruler:
    - Arduino Uno PCB: 68.6mm × 53.3mm
    - LCD1602 with adapter: 80mm × 36mm (display area: 75mm × 27mm)
    - Your specific keypad dimensions (typically 69mm × 77mm active area)
    - Joystick module footprint
    - DHT11 module footprint
    - Relay module footprint
2. **Design in layers.** Start with the Arduino mounting plate, then build walls around it, then add the top with cutouts.
3. **Print a test fit first.** Before printing the full enclosure at 20% infill with 6 walls, print just the LCD bezel cutout section at 0.3mm layer height (fast, low quality). Test fit with the actual LCD. Adjust dimensions, then print final.
4. **Wall thickness:** Minimum 2mm for structural walls, 3mm for walls with features (cutouts, clips).
5. **Print orientation:** Print the main body with the top face DOWN on the bed. This gives you the best surface finish on the most visible face.

---

### Step 9.4 — Assembly After Printing

1. Secure Arduino to mounting posts with M3 screws (4mm stainless steel) or use hot glue on the standoffs.
2. Run all wires before final assembly — you cannot rewire easily once enclosed.
3. Use zip ties or printed cable clips inside to route wires neatly.
4. The LCD sits in the top bezel cutout and is held by a printed retaining clip or small amount of hot glue at the corners.
5. The LED dome snaps or glues over the LED bank — leave it accessible for LED replacement.
6. The joystick button cap press-fits. Test the fit before gluing — it should be snug but not permanent.
7. The DHT11 module should be positioned so its sensor face aligns with the vent holes. Do not enclose it in a sealed cavity.

---

### Step 9.5 — 3D Printing Gotchas

**Warping:** If PLA corners lift off the bed during printing, use a brim (3–5mm) in your slicer settings and ensure the bed is properly leveled and at ~60°C.

**Overhangs:** The LCD cutout and dome may have overhangs. Keep overhangs under 45° without supports, or enable supports in your slicer for features that need them.

**Tolerances:** 3D printers are accurate to ±0.2–0.5mm. For things that need to fit together (snap fits, press fits), design with 0.3–0.4mm clearance on each mating surface. For things that slot into cutouts (LCD), add 0.5mm to each dimension.

**First layer adhesion:** If first layer doesn't stick, the bed is too far from the nozzle or the surface is dirty (clean with isopropyl alcohol).

---

## PHASE 10 — Polish and Portfolio

**Goal:** Clean code, clean hardware, documentation, and a portfolio-worthy presentation of the project.

---

### Step 10.1 — Code Cleanup

- Add comments to every function explaining what it does and why
- Remove all debug `Serial.println()` calls (or wrap them in `#ifdef DEBUG` conditional compilation)
- Ensure variable names are descriptive (`alarmHour`, not `ah` or `x`)
- Move configuration constants to the top of the file as `#define` or `const` values:

cpp

  `#define SUNRISE_DEFAULT_MINUTES 20
  #define SLEEP_DEBT_TARGET_HOURS 8
  #define LDR_THRESHOLD 400
  #define JOYSTICK_HOLD_MS 3000`

- Group related code into `.h` / `.cpp` files or at minimum into clearly labeled sections with comment headers

---

### Step 10.2 — Wiring Cleanup

Once the electronics are in the enclosure:

- Cut all jumper wires to the exact length needed (no loose wire bundled up inside)
- Use twist-ties or printed clips to bundle parallel wires
- Verify all connections are secure by gently tugging each wire
- Apply a small dab of hot glue to the DHT11 connector and joystick connector so they can't work loose during use

---

### Step 10.3 — Portfolio Documentation

For a strong engineering portfolio presentation, produce:

**1. A short demo video (60–90 seconds)**

- Show the device turning on (clock displays)
- Show entering sleep mode (one press, backlight off)
- Fast-forward or narrate through a "simulated night" with the alarm set 2 minutes ahead
- Show the sunrise LED ramp-up (slow → faster, clearly non-linear)
- Show the alarm melody escalating
- Dismiss the alarm, cycle through the morning report screens
- Show the settings menu briefly

**2. A photo set (6–8 photos)**

- Top-down overview (shows all components)
- 3/4 angle of finished enclosure
- Close-up of LCD showing clock
- Close-up of LCD showing morning report
- Sunrise LEDs glowing (shoot in a dim room)
- Internal photo showing wiring (shows engineering depth)

**3. A one-page project brief** covering:

- Problem it solves (phone-free sleep tracking)
- Key design decisions and why (I2C bus sharing, cosine brightness curve, joystick 3-second hold, EEPROM circular buffer)
- Components used and their roles
- What you'd change with more time/resources (DS3231 for better accuracy, BLE for data export, custom PCB)

---

### Master Timeline (4 Weeks)

`WEEK 1: Foundation
─────────────────
Day 1–2:   Phase 0 (Environment setup, order I2C adapter)
Day 3–4:   Phase 1, steps 1.1–1.6 (LCD, RTC, DHT11, Keypad, Buzzer, Relay)
Day 5–6:   Phase 1, steps 1.7–1.10 (LEDs, LDR, Joystick, Scanner)
Day 7:     Phase 2 (Clock + Display working)

WEEK 2: Core Logic
──────────────────
Day 8–9:   Phase 3 (Sleep logging, debt calculation)
Day 10–11: Phase 4 (DHT11 + LDR sampling, morning report screens)
Day 12:    Buffer / review / fix integration issues
Day 13–14: Phase 5.1–5.3 (Sunrise LED ramp, alarm melody stages)

WEEK 3: Full System
───────────────────
Day 15:    Phase 5.4–5.5 (Relay, alarm without sleep mode)
Day 16–18: Phase 6 (Settings menu, keypad UI)
Day 19:    Phase 7 (EEPROM persistence, boot/save/load)
Day 20:    Phase 8.1–8.2 (State machine, integration test)
Day 20–21: 3D design starts (parallel — design while testing)

WEEK 4: Finish
──────────────
Day 22–23: Phase 8.3–8.4 (Bug fixes, overnight soak test)
Day 24–26: Phase 9 (3D printing, assembly)
Day 27–28: Phase 9.4–9.5 (Final assembly, finishing)
Day 29–30: Phase 10 (Code polish, photos, video, portfolio writeup)`

---

### Risks, Struggles, and Watchouts

### HIGH RISK

**Pin budget is exact.** The allocation in this PRD accounts for every pin on the Uno. Do not add components without first checking the pin map. If you want to add something (e.g., a second button), check if an analog pin is free or if a digital function can be time-multiplexed.

**I2C address conflict detection.** If your PCF8574 adapter is set to 0x68 (the RTC's address), both devices will conflict on the bus. The adapter's address is set by solder bridges or jumpers on the PCF8574 module. If conflict occurs, consult the PCF8574 datasheet for how to change the address solder bridge. Almost all units default to 0x27 — different from RTC's 0x68 — so conflict is unlikely but check with the scanner from Step 1.1.

### MEDIUM RISK

**RTC time drift.** The DS1307 can drift 1–2 minutes per month at room temperature. For a sleep tracker, this is acceptable. But if you add functionality that depends on precise timing over weeks, consider upgrading to a DS3231 module (better accuracy, same I2C interface, same RTClib library, direct hardware swap).

**DHT11 self-heating from circuit.** If the DHT11 is enclosed too close to the Arduino and relay (which generate a small amount of heat), the temperature readings will read 1–3°C higher than actual room temperature. The vent holes in the enclosure (Step 9.2) and placing the DHT11 on the side of the enclosure (not the top) mitigate this.

**Keypad pin numbering.** Every 4×4 keypad has its ribbon cable in a slightly different order. Never assume — always verify every key with the test sketch (Step 1.5). If keys come out wrong, swap the rowPins or colPins arrays in your code until the matrix matches the physical layout.

### LOWER RISK / GOOD TO KNOW

**EEPROM.update() vs EEPROM.write().** Always use `update()`. The `write()` function writes unconditionally, consuming one of your ~100,000 write cycles even if the value hasn't changed. `update()` checks first. Over a year of daily sleep logging (~365 writes), this matters far less than it would for a write-heavy application, but the habit is correct.

**The Keypad library's `getKey()` is non-blocking.** It returns `NO_KEY` (a `char` value 0) if nothing is pressed. It only returns a key value at the moment of press, not while held. If you need hold-detection for the keypad (e.g., fast-increment while holding `#`), use `getState()` and check for `HOLD` state.

**`tone()` and `noTone()` are global.** Only one tone plays at a time across the entire Arduino. If you call `tone(3, 523)` and then call `tone(3, 659)` before the first finishes, the second immediately overrides it. Use the duration parameter or manage timing manually.

**Serial Monitor vs. uploaded device.** While debugging, Serial.println() is invaluable. But remember: the Serial Monitor only works when connected via USB. In standalone operation (battery or wall power), it does nothing. Don't leave important logic dependent on Serial — confirm everything works with USB unplugged before considering it done.

**Power supply for standalone operation.** During development, you power the Uno via USB from your Mac. The kit includes a power supply module that works with the breadboard. For nightstand use, a 5V USB wall charger with a USB-A to USB-B cable (the Uno's connector) gives clean standalone power. A 9V power supply via the barrel jack on the Uno also works (the Uno's onboard regulator drops it to 5V), but generates more heat. USB power is preferred.

---

### Definition of Done

DAWNTRACE is complete when:

- A user can plug it in, hold the joystick for 3 seconds at bedtime, and wake up to a progressive light and melody at their programmed alarm time, followed by a morning report of their sleep duration and bedroom conditions — **without touching their phone at any point.**
- All settings persist through a power cycle.
- 30 nights of sleep data accumulate in EEPROM and display a weekly trend on the report screen.
- The device lives in a 3D printed enclosure that looks like a finished product, not a breadboard experiment.
- You can explain every component's role, every wiring decision, and every software pattern to someone reviewing your engineering portfolio.
