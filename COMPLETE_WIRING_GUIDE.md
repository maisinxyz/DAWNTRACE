# Complete DAWNTRACE Circuit Wiring Guide

This document contains the complete wiring guide for the DAWNTRACE circuit, specifically including the driver circuit needed for a raw 5V relay. The microcontroller used is an **ELEGOO Uno R3** (or any standard Arduino Uno R3). 

## ⚡ 5V Relay (For Bedroom Lamp)
*Important: Because you are using a **raw 5V relay** and not a module, you MUST use a driver circuit. An Arduino pin cannot supply enough current for the relay coil, and the inductive kickback will damage the board.*

**Identifying Transistor Legs (PN2222):** Hold the transistor so the **flat side is facing you** and the legs point down.
* **Left Leg** = Emitter
* **Middle Leg** = Base
* **Right Leg** = Collector

**Wiring:**
- **Transistor Base (Middle Leg)** ➡️ Pin 4 *(Connect via a ~1kΩ base resistor)*
- **Transistor Emitter (Left Leg)** ➡️ GND
- **Transistor Collector (Right Leg)** ➡️ One side of the Relay Coil
- **Other side of Relay Coil** ➡️ 5V
- **Flyback Diode (e.g., 1N4001 or 1N4148)** ➡️ Connected across the relay coil. The striped end (Cathode) goes to 5V, and the non-striped end (Anode) goes to the Transistor Collector.

## 🖥️ Display (LCD1602 Parallel)
*You are using parallel mode for the LCD instead of I2C. A standard LCD1602 has 16 pins. Here is how to wire all of them:*

**Power & Backlight:**
- **Pin 1 (VSS)** ➡️ GND
- **Pin 2 (VDD)** ➡️ 5V
- **Pin 15 (A / Anode)** ➡️ 5V *(Provides power to the backlight)*
- **Pin 16 (K / Cathode)** ➡️ GND

**Contrast Potentiometer (Usually 10kΩ):**
- **Potentiometer Left Leg** ➡️ 5V
- **Potentiometer Right Leg** ➡️ GND
- **Potentiometer Middle Leg (Wiper)** ➡️ **LCD Pin 3 (V0)**

**Data & Control Pins:**
- **LCD Pin 4 (RS)** ➡️ Arduino Pin 8
- **LCD Pin 5 (RW)** ➡️ GND *(We wire this to GND because we only write to the screen, never read from it)*
- **LCD Pin 6 (EN)** ➡️ Arduino Pin 9
- **LCD Pin 11 (D4)** ➡️ Arduino Pin 10
- **LCD Pin 12 (D5)** ➡️ Arduino Pin 11
- **LCD Pin 13 (D6)** ➡️ Arduino Pin 12
- **LCD Pin 14 (D7)** ➡️ Arduino Pin 13

## ⏱️ Real-Time Clock (DS1307 RTC)
*Connects via the I2C bus.*
- **SDA** ➡️ Pin A4
- **SCL** ➡️ Pin A5
- **VCC** ➡️ 5V
- **GND** ➡️ GND

## 🌡️ Temperature & Humidity Sensor (DHT11)
- **Data (Out)** ➡️ Pin 2
- **VCC** ➡️ 5V
- **GND** ➡️ GND

## 🔊 Passive Buzzer
- **Signal (+)** ➡️ Pin 3
- **GND (-)** ➡️ GND

## 🌅 Sunrise LEDs
*These are also driven via a transistor (since LEDs draw more current than Arduino pins can provide).*

**Identifying Transistor Legs (PN2222):** Hold the transistor so the **flat side is facing you** and the legs point down.
* **Left Leg** = Emitter
* **Middle Leg** = Base
* **Right Leg** = Collector

**Wiring:**
- **Transistor Base (Middle Leg)** ➡️ Pin 5 (PWM) *(Connect via a ~1kΩ base resistor)*
- **Transistor Collector (Right Leg)** ➡️ Connect to the **Short Leg (Cathode)** of the Sunrise LEDs.
- **Transistor Emitter (Left Leg)** ➡️ GND
- **Sunrise LEDs Long Leg (Anode)** ➡️ Connect to a **Current Limiting Resistor** (e.g., 220Ω), and then connect the other side of that resistor to **5V**.

## 🔴 Red Status LED
- **Anode (+ long leg)** ➡️ Pin 6 *(Include a current-limiting resistor, e.g., 220Ω)*
- **Cathode (- short leg)** ➡️ GND

## 🎮 IR Receiver (VS1838B)
*Note: This replaces the old joystick setup.*
- **Signal (S/Y)** ➡️ Pin 7
- **VCC (R)** ➡️ 5V
- **GND (G)** ➡️ GND

## ☀️ Light Sensor (LDR / Photoresistor)
*A "voltage divider" isn't a single part you buy—it's a small circuit you build yourself using the LDR and a standard fixed resistor (you are using a **1kΩ** resistor).*
To wire it:
1. Connect one leg of the **LDR** to **5V**.
2. Connect the other leg of the **LDR** to **Pin A2**.
3. Connect one leg of your **1kΩ Resistor** to **Pin A2** (sharing the same row as the LDR leg).
4. Connect the other leg of the **1kΩ Resistor** to **GND**.
