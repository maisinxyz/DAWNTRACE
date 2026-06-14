Phases 1 - 4:
    Starting page:
    - Displays time, day, date, and pre-set alarm time (7AM) hold joystick 3s to go to sleeping page.
    to do:
    1. Add page that allows user to set alarm
    2. Change time to 12 hour clock and add AM/PM
    3. for future, add animations and make look nice. 

    Sleeping page: 
    - Displays ZZZ, more subtle than the other page, less info. 
    - Displays time temp, humidity, and light intrusion.
    to do:
    1. Ensure all sensors work and display correctly.
    2. For future, make look better.

    Wake Up page:
    - displays time slept, and time woken up
    to do:
    1. Add time user went to sleep.
    2. for future, add a cool graphic and animation.

    Stats page 1:
    - Displays Avg temp and avg humidity.
    to do:
    1. Ensure all sensors work
    2. for future, possibly combine stats pages 1, 2, 3 together.

    Stats page 2:
    - Displays Light intrustion count and "Night" varaible, determines if the night was dark or not depending on numbner of light events.
    to do:
    1. ensure light sensor worked, and add a more concrete definition of a "light event".
    2. In the future, add the time for ecah light event, and a graph of light as the night went on.

    Stats page 3:
    - Displays sleep debt (how many hours requried for target) and pattern in comparison to other sleep logs.
    to do:
    1. for future, add graphs of consistency, graphs that show sleep times, "light events", hours slept, trends etc.

    Page 4:
    - Logs sleep.
    - for future, add a sound/ animation.




Phases 5 - 8:

    1. Sequence (Phase 5)
    - 20-Minute Sunrise Curve: white LEDs turn on 20 minutes before your alarm time and slowly fade up 
    - 3-Stage Progressive Melody: buzzer plays gentle 3-note chime every 10 seconds. Then every 5 seconds. Finally, if you don't wake up, it switches to a rapid, continuous alarm.
    - Lamp Control: At the exact alarm time, the Relay needs to click on to turn on your actual bedroom lamp.

    2. The Settings Menu (Phase 6)
    - Currently, wak up time (7AM) is hardcoded, need to add menu to change.
    - Settings to include: Alarm Time, Sunrise Duration, Target Sleep Hours, Alarm ON/OFF toggle, and LDR (light sensor) sensitivity threshold.
    - Joystick Redesign: Add more inputs, joy stick is not enough.

    3. Saving Settings to Memory (Phase 7)
    - We already built the logic that saves your sleep logs to the EEPROM (permanent memory), but we need to do the same for your settings.
    - alarm was cahgned to 8:00 AM, and Arduino loses power, it should remember alarm time affter booting back on.
    - Add sleep/ conditions info to cloud or some data base.

    4. Full Integration (Phase 8)
    Tying everything together seamlessly, ensuring there are no bugs when transitioning from the Menu back to Sleep Mode, and making sure the alarm can still fire even if you forgot to put the clock into "Sleep Mode" the night before.

End Game:
    1. Make 3D printed casing for whole thing
    2. Buy some new components (better sensors, better screen, more inputs.)
    3. Order a custom PCB, and solder (can do at media and maker commons) parts on.
    4. Make a phone app to connect to and upload data to cloud.
    5. Add power source, charging port or batteries.
    6. Make project a FINAL PRODUCT! (actually a good product.)
    


Wish list:
1. A better screen to display more info on.
2. 3D casing
3. Wifi connectivity to connect to a phone app.
4. Connect to lamps, smart lights, and upload data to the cloud.

use this to run:
pio run -t upload

to do: 
- Review all features and functionality of sensors
- Fix IR remote inputs and infinite hardware check loop.
- Work on todo list for phase 1-4. 
