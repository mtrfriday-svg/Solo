# Solo test build – ONE ESP32-S3 N16R8 does everything

Use this while the second board (body) is missing. Build name: **mdee-solo**. GitHub builds it automatically on every push.
**Not compiled or run on hardware yet** – expect a few small fixes.

## What works / what doesn't in the solo build
| Works | Not in this build |
|---|---|
| Wi-Fi, AI voice (mic + speaker), face on the 1.54" screen, boot logo | Servos (neck + ears): no free pins |
| 2 motors sides via TB6612, speed levels, obstacle stop | Only **4** white LEDs (2 left, 2 right) instead of 8 |
| Front + rear ultrasonic sensors, wander and follow modes | Second board link (not needed) |
| 2 NeoPixels, status colours | |
| OLED status (new roomier layout), phone control page | |
| Logic-battery meter (GPIO1) and motor-battery meter (GPIO2) | |

The AI's neck/ears commands are accepted but do nothing here.

## Why the phone page failed before (and what I changed)
1. **Wi-Fi power saving (most likely cause).** The XiaoZhi firmware puts Wi-Fi into deep sleep whenever the robot is idle. A sleeping ESP32 can't be reached from a phone, so the page did not load or timed out, while talking to the AI still worked. The robot now keeps Wi-Fi fully awake.
2. **No body board.** In the two-board build the page's buttons send commands to the second board. With that board missing, nothing moves and the status never updates. The page now shows a red "Body board not connected" message, and the solo build has no such board.
3. **Sockets.** The web server could take connections the AI needs. It now uses at most 5, and the system has 16 available.
4. The page's JavaScript was checked for syntax errors: none.

It does **not** conflict with the AI connection: they are separate connections on the same Wi-Fi.

## Wiring (N16R8)
| Part | Part pin | GPIO |
|---|---|---|
| INMP441 mic | SCK / WS / SD | 4 / 5 / 6 (L/R and GND to GND, VDD to 3.3 V) |
| MAX98357A amp | BCLK / LRC / DIN | 4 / 5 / 7 (VIN 5 V) |
| **1.54" ST7789 240×240** | SCL / SDA / CS / DC / RST | 12 / 11 / 10 / 9 / 8 |
| | BL, VCC, GND | BL to 3.3 V, VCC 3.3 V, GND |
| OLED 0.96" | SDA / SCL | 17 / 18 (3.3 V) |
| TB6612 left motors | AIN1 / AIN2 | 38 / 39 |
| TB6612 right motors | BIN1 / BIN2 | 40 / 41 |
| TB6612 | STBY | **42**, with 10 kΩ from STBY to GND |
| TB6612 | **PWMA and PWMB** | **both to 3.3 V** (speed is made on the IN pins) |
| TB6612 | VCC / VM / GND | 3.3 V / 2S pack (7.4 V) / GND |
| HC-SR04 ×2 | TRIG (both joined) | 13 |
| | front ECHO | 14 through the 1 kΩ / 2 kΩ divider |
| | rear ECHO | 3 through the 1 kΩ / 2 kΩ divider |
| NeoPixel ×2 (chained) | DIN | 47 through 330 Ω |
| White LEDs ×4 | left 1, left 2 | 15, 16 |
| | right 1, right 2 | 21, 48 |
| | each LED | GPIO → 330 Ω → LED anode, cathode → GND |
| Logic battery divider (100 kΩ / 100 kΩ + 100 nF) | middle | 1 |
| Motor battery divider (100 kΩ / 22 kΩ + 100 nF, 2S pack) | middle | 2 |
| BOOT button | built in | 0 |

* GPIO 48 is the on-board RGB LED on some N16R8 boards: it will glitch with the LED. Harmless; if it bothers you, tell me and I'll move it.
* Join all grounds. 5 V from the mini boost to the board's 5V pin, amp, sensors and NeoPixels; motors straight from the 2S pack.
* Don't have USB plugged in while 5 V also feeds the board's 5V pin, unless your board has protection.
* Echo dividers: ECHO → 1 kΩ → GPIO, GPIO → 2 kΩ → GND.

## Settings you may need to change (`main/boards/mdee-robot/config.h`, solo section)
| Problem | Setting |
|---|---|
| A wheel side runs backwards | `SOLO_MOTOR_LEFT_INVERT` or `SOLO_MOTOR_RIGHT_INVERT` to `true` |
| Face shifted by 80 rows | `DISPLAY_OFFSET_Y 80` |
| Face mirrored or rotated | `DISPLAY_MIRROR_X/Y`, `DISPLAY_SWAP_XY` |
| Red and blue swapped | `DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR` |
| Motor-battery divider not wired yet | `SOLO_MOTOR_BAT_MONITOR 0` |
| Cut motors when the 2S pack is low | `SOLO_MOTOR_BAT_CUTOFF 1` (only after the divider is wired and calibrated; default 0 = never cut) |
| Robot too slow or too weak | `SPEED_*_PCT` (solo defaults 45 / 65 / 85 / 100) |
| Stop and slow-down distance | `SOLO_OBSTACLE_STOP_CM` 18, `SOLO_OBSTACLE_SLOW_CM` 40 |

## New OLED layout (6×9 pixel font, spaced lines)
```
|||. Listening...           <- Wi-Fi bars + what the robot is doing
M7.4V 87% L3.9V 72%         <- M motor pack, L logic pack
F123 R45 manual             <- front/rear distance in cm, mode
-----------------
You: what is the time        <- 4 lines of conversation
M'DEE: it is ...
```
Idle with no conversation: it shows the **phone page address** (http://IP). `NO BODY LINK!` blinks only in the two-board build.

## Build and flash
1. Put the new zip in Download, then in Termux go to your project folder (the one containing `termux_push.sh`):
```
cd ~
mkdir -p newzip && cd newzip
unzip -o ~/storage/downloads/Bot2-mdee.zip
cp -a Bot2-main/. ~/YOUR_PROJECT_FOLDER/
cd ~ && rm -rf newzip
cd ~/YOUR_PROJECT_FOLDER
bash termux_push.sh "solo test build"
```
(Replace `YOUR_PROJECT_FOLDER` with the folder name you pushed from.)
2. GitHub → Actions → the run builds **one** job, "Solo test build". Download `mdee_solo_n16r8_…` and flash `merged-binary.bin` at address 0x0 on the N16R8 (erase flash first).
3. To build the two-board brain or the body later: Actions → Build M'DEE robot → Run workflow → choose brain, body or all.

## Test in this order
1. **USB power only** (no motor pack): boot logo (~4 s), face, OLED rows. Join the hotspot, set Wi-Fi, activate with the code.
2. OLED shows `Phone page: http://IP`. On the phone (same Wi-Fi, mobile data off) open it with `http://` typed in. In Termux you can test it with `curl -s http://IP/api/status`. It should print JSON with `"link":true`.
3. Tap the LED buttons: 4 LEDs run On / Chase / Blink / signals. Tap the colour buttons: NeoPixels change.
4. Wave a hand in front of each sensor: OLED `F` and `R` change.
5. Connect the **2S motor pack with the wheels lifted**. Drive with the joystick at Slow. If a side is backwards, flip the invert flag.
6. Try Wander and Follow. Say "go forward slowly", "turn left", "stop".
7. Check the logic-battery number against a multimeter.

If the page still fails: tell me (a) what the OLED shows for the phone page address, (b) whether `curl` in Termux works, (c) what the phone shows (blank, timeout, or page without response).

## Later: ESP32-CAM, bigger face, status screen
* **ESP32-CAM for web control + video:** possible. The usual setup serves the control page on port 80 and the video stream on port 81. Control commands are tiny, so they don't fight with the video. Real risks: **power** (needs a steady 5 V, 500 mA or more, with a capacitor; brownouts are the number one problem), keeping video to about 640×480 or smaller so the page stays quick, and the board has no USB (needs a programmer board). It would send the commands to the robot over 3 wires (UART). I'll write that once your board and parts arrive.
* **1.69" face + 1.54" as status screen:** I'll do it when the 1.69" arrives. The status screen is drawn separately with larger text and colour battery bars, so the OLED becomes unnecessary.
* **Second N16R8 as the body:** the body firmware must be re-targeted to it (different pins, 16 MB flash). I'll prepare that too.
