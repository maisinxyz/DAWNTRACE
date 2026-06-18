# DAWNTRACE

A bedside device that tracks sleep schedules and bedroom environments, waking the user with a gradual sunrise light, escalating alarm melody, and a post-sleep analytical report. Designed to replace the smartphone as a bedside alarm. 

## CORE FEATURES

* **Sleep Logging:** Records sleep start, wake time, and calculates sleep debt across a 30-day rolling log stored in EEPROM.
* **Environmental Monitoring:** Tracks average room temperature, humidity, and light intrusion events during the sleep cycle.
* **Progressive Wake Sequence:** Executes a cosine-based ease-in brightness ramp for external LEDs, followed by a three-stage escalating audio alarm.
* **External Device Control:** Built-in 5V relay to trigger external lighting or appliances at the scheduled alarm time.
* **IR Remote Operation:** Fully controllable via an infrared remote, eliminating the need for physical device interaction.

## SYSTEM ARCHITECTURE

DAWNTRACE operates on an ATmega328P microcontroller (Arduino Uno) utilizing a strict state machine pattern. It heavily relies on time-based polling via `millis()` to ensure non-blocking execution of environmental sampling, audio generation, and display updates. 

Memory management is handled explicitly. The system utilizes 250 bytes of the available 1,024 bytes of internal EEPROM for settings and a circular buffer of the last 30 sleep entries.

## HARDWARE SPECIFICATIONS

The system requires an Arduino Uno R3 or equivalent ATmega328P development board.

### Pin Allocation Map

| Component | Arduino Pin | Function |
| :--- | :--- | :--- |
| **DHT11 Sensor** | D2 | Temperature and humidity sampling |
| **Passive Buzzer** | D3 (PWM) | Alarm melody generation via `tone()` |
| **5V Relay Module** | D4 | External lamp or device control |
| **Sunrise LEDs** | D5 (PWM) | Brightness ramp via PN2222 NPN Transistor |
| **Red Status LED** | D6 (PWM) | System status indicator |
| **VS1838B IR Receiver** | D7 | NEC protocol signal reception |
| **LCD RS** | D8 | LCD Register Select |
| **LCD EN** | D9 | LCD Enable |
| **LCD D4 to D7** | D10 to D13 | LCD Data Bus |
| **LDR (Photoresistor)** | A2 | Ambient light intrusion monitoring |
| **DS1307 RTC** | A4 (SDA), A5 (SCL) | I2C Real-Time Clock data bus |

## SOFTWARE DEPENDENCIES

Include the following libraries in your build environment prior to compilation:

* `IRremote` (v4.x)
* `DHT sensor library`
* `EEPROM` (Built-in)
* `LiquidCrystal`
* `RTClib`
* `Wire` (Built-in)

## IR REMOTE INTERFACE

DAWNTRACE is operated exclusively via an IR remote using the NEC protocol. 

| Input Button | Function / Action |
| :--- | :--- |
| **2** | Toggle Sleep Mode |
| **3** | Cycle through morning report pages |
| **4** | Enter Settings Menu / Confirm selection |
| **5** | Snooze alarm for 5 minutes |
| **0** | Snooze alarm for 10 minutes |
| **7 / 9** | Decrease / Increase values in Settings |
| **#** | Save settings and exit menu |
| **\*** | Discard settings changes and exit |
| **OK / D-Pad** | Dismiss active alarm |
| **1** | Execute soft system reset |

## FUTURE DEVELOPMENT

The current hardware prototype serves as a foundational baseline. Planned iterations for DAWNTRACE include:

* **Hardware Enclosure:** Fabrication of a 3D printed brutalist housing, including specific cutouts for the parallel LCD, an isolation dome for the LED array, and adequate ventilation for accurate DHT11 thermals.
* **Component Upgrades:** Transitioning from the DS1307 RTC to the highly precise DS3231 module to eliminate inherent monthly time drift.
* **Connectivity:** Integration of a BLE module to allow bulk export of the 30-day EEPROM sleep ledger for external data analysis. 
* **Custom PCB:** Consolidation of the breadboard prototype into a unified, mixed-technology printed circuit board.
