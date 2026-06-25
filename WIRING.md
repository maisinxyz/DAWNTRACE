# DAWNTRACE Circuit Wiring Guide

Based on the configuration in `DAWNTRACE/src/main.cpp`, here is the complete wiring guide for the DAWNTRACE circuit using an **ELEGOO Uno R3** (or any standard Arduino Uno R3). 

## 🖥️ Display (LCD1602 Parallel)
*You are using parallel mode for the LCD instead of I2C.*
- **RS** ➡️ Pin 8
- **EN** ➡️ Pin 9
- **D4** ➡️ Pin 10
- **D5** ➡️ Pin 11
- **D6** ➡️ Pin 12
- **D7** ➡️ Pin 13

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

## ⚡ 5V Relay (For Bedroom Lamp)
*Since you have a **raw 5V relay** (not a module), you CANNOT connect it directly to the Arduino pin. You must use a transistor and a flyback diode.*
- **Transistor Base (e.g., PN2222)** ➡️ Pin 4 *(via a ~1kΩ resistor)*
- **Transistor Emitter** ➡️ GND
- **Transistor Collector** ➡️ One side of the Relay Coil
- **Other side of Relay Coil** ➡️ 5V
- **Flyback Diode (e.g., 1N4001)** ➡️ Connected across the relay coil. The striped end (Cathode) goes to 5V, and the other end (Anode) goes to the Transistor Collector.

## 🌅 Sunrise LEDs
*These are driven via a **PN2222 transistor** (since LEDs draw more current than Arduino pins can provide).*
- **Base of PN2222** ➡️ Pin 5 (PWM) 
*(Note: Be sure to put a resistor between Pin 5 and the transistor base, and connect the LEDs to the Collector of the transistor with the Emitter going to GND).*

## 🔴 Red Status LED
- **Anode (+ long leg)** ➡️ Pin 6 *(Include a current-limiting resistor, e.g., 220Ω)*
- **Cathode (- short leg)** ➡️ GND

## 🎮 IR Receiver (VS1838B)
*Note: This replaces the old joystick setup.*
- **Signal (S/Y)** ➡️ Pin 7
- **VCC (R)** ➡️ 5V
- **GND (G)** ➡️ GND

## ☀️ Light Sensor (LDR / Photoresistor)
*This requires a voltage divider circuit.*
- **Voltage Divider Output** ➡️ Pin A2
*(Wire the LDR from 5V to A2, and a pull-down resistor from A2 to GND).*
