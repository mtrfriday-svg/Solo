#ifndef _MDEE_CONFIG_H_
#define _MDEE_CONFIG_H_

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

// Two builds share this file:
//   mdee-robot : BRAIN of the two-board robot (body = second ESP32-S3 over UART)
//   mdee-solo  : ONE ESP32-S3 N16R8 does everything (test build)  -> CONFIG_MDEE_SOLO=1

// ===================== AUDIO (shared I2S bus, duplex) =====================
// INMP441 mic + MAX98357A amp share BCLK and WS. INMP441: L/R -> GND, VDD -> 3V3.
// MAX98357A: VIN -> 5V, GAIN/SD left default (SD floating = enabled, L+R/2).
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_4
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6   // INMP441 SD
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7   // MAX98357A DIN

// ===================== FACE DISPLAY (SPI ST7789) =====================
// BL pin tied to 3V3.
#define DISPLAY_MOSI_PIN GPIO_NUM_11
#define DISPLAY_CLK_PIN  GPIO_NUM_12
#define DISPLAY_CS_PIN   GPIO_NUM_10
#define DISPLAY_DC_PIN   GPIO_NUM_9
#define DISPLAY_RST_PIN  GPIO_NUM_8
#define DISPLAY_SPI_MODE 0
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#if CONFIG_MDEE_SOLO
// 1.54" 240x240 ST7789 (test display). If the picture is shifted by 80 rows, set DISPLAY_OFFSET_Y 80.
// If it is mirrored/rotated change the MIRROR/SWAP lines. If red and blue are swapped use LCD_RGB_ELEMENT_ORDER_BGR.
#define DISPLAY_WIDTH    240
#define DISPLAY_HEIGHT   240
#define DISPLAY_SWAP_XY  false
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#else
// 1.69" 240x280 ST7789 in landscape. If the face is rotated/mirrored/shifted, tweak these lines only.
#define DISPLAY_WIDTH    280
#define DISPLAY_HEIGHT   240
#define DISPLAY_SWAP_XY  true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_OFFSET_X 20
#define DISPLAY_OFFSET_Y 0
#endif

// ===================== I2C bus: 0.96" OLED (0x3C) only =====================
#define I2C_SDA_PIN GPIO_NUM_17
#define I2C_SCL_PIN GPIO_NUM_18
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Speed levels (percent). In the two-board robot keep identical to mdee_body/body_config.h
#if CONFIG_MDEE_SOLO
#define SPEED_SLOW_PCT   45
#define SPEED_MEDIUM_PCT 65
#define SPEED_FAST_PCT   85
#define SPEED_TURBO_PCT  100
#else
#define SPEED_SLOW_PCT   35
#define SPEED_MEDIUM_PCT 55
#define SPEED_FAST_PCT   75
#define SPEED_TURBO_PCT  95
#endif

// ===================== LOGIC BATTERY monitor (2 x Li-ion in parallel, measured at the terminals) =====================
// Divider on GPIO1 (ADC1_CH0): battery+ --[R1 100k]--+--[R2 100k]-- GND, 100 nF from pin to GND. Ratio 0.5.
#define LOGIC_BAT_ADC_UNIT     ADC_UNIT_1
#define LOGIC_BAT_ADC_CHANNEL  ADC_CHANNEL_0
#define LOGIC_BAT_R_UPPER_OHM  100000.0f
#define LOGIC_BAT_R_LOWER_OHM  100000.0f
#define LOGIC_BAT_FULL_V       4.20f
#define LOGIC_BAT_EMPTY_V      3.30f    // 0 % (kept above the 3.0 V cell limit for safety)
#define LOGIC_BAT_LOW_PCT      15       // warning + beep
#define LOGIC_BAT_CRITICAL_V   3.20f    // below this for 5 s: robot stops, ears droop

// ===================== LED BAR: white LEDs wired DIRECTLY to GPIOs (no PCF8574 needed) =====================
// Wiring per LED: GPIO --[330 ohm]--> LED anode, LED cathode --> GND (active high).
// First half of the list = LEFT side, second half = RIGHT side (turn signals use these halves).
#define LED_BAR_ON_BRAIN    1
#define LED_BAR_ACTIVE_HIGH 1
#if CONFIG_MDEE_SOLO
#define LED_BAR_COUNT 4
#define LED_BAR_PINS { GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_21, GPIO_NUM_48 }
#else
#define LED_BAR_COUNT 8
#define LED_BAR_PINS { GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_21, GPIO_NUM_38, \
                       GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42 }
#endif

#if CONFIG_MDEE_SOLO
// ===================== SOLO BUILD: body hardware on this same board =====================
// TB6612FNG. PWMA and PWMB are wired to 3V3 (always on); speed = PWM on the IN pins. STBY has its own pin.
#define SOLO_MOTOR_AIN1 GPIO_NUM_38    // left motors
#define SOLO_MOTOR_AIN2 GPIO_NUM_39
#define SOLO_MOTOR_BIN1 GPIO_NUM_40    // right motors
#define SOLO_MOTOR_BIN2 GPIO_NUM_41
#define SOLO_MOTOR_STBY GPIO_NUM_42    // + 10 kOhm to GND so motors are off while booting
#define SOLO_MOTOR_LEFT_INVERT  false
#define SOLO_MOTOR_RIGHT_INVERT false

// 2 x HC-SR04, shared TRIG. Each ECHO through a 1k/2k divider (5 V -> 3.3 V).
#define SOLO_US_TRIG_PIN       GPIO_NUM_13
#define SOLO_US_ECHO_FRONT_PIN GPIO_NUM_14
#define SOLO_US_ECHO_REAR_PIN  GPIO_NUM_3
#define SOLO_OBSTACLE_STOP_CM  18
#define SOLO_OBSTACLE_SLOW_CM  40

// 2 x NeoPixel
#define SOLO_NEOPIXEL_PIN   GPIO_NUM_47
#define SOLO_NEOPIXEL_COUNT 2

// Motor battery (2S Li-ion 6.0-8.4 V) divider on GPIO2 (ADC1_CH1): pack+ --[100k]--+--[22k]-- GND, 100 nF at the pin.
// SOLO_MOTOR_BAT_MONITOR 0 = do not read it at all (use when the divider is not wired yet).
// SOLO_MOTOR_BAT_CUTOFF 1 = cut the motors when the pack is nearly empty (turn on after the divider works).
#define SOLO_MOTOR_BAT_MONITOR   1
#define SOLO_MOTOR_BAT_CUTOFF    0
#define SOLO_MOTOR_BAT_CHANNEL   ADC_CHANNEL_1
#define SOLO_MOTOR_BAT_R_UPPER_OHM 100000.0f
#define SOLO_MOTOR_BAT_R_LOWER_OHM 22000.0f
#define SOLO_MOTOR_BAT_EMPTY_V   6.6f
#define SOLO_MOTOR_BAT_FULL_V    8.4f
#define SOLO_MOTOR_BAT_CAL_FACTOR 1.0f
#endif

// ===================== BUTTON =====================
#define BOOT_BUTTON_GPIO GPIO_NUM_0

// ===================== WEB CONTROL =====================
#define WEB_CONTROL_PORT 80

#if !CONFIG_MDEE_SOLO
// ===================== LINK to the body (second ESP32-S3) =====================
// Cross-wire: brain TX(15) -> body RX, brain RX(16) <- body TX, GND <-> GND.
#define BODY_LINK_UART   UART_NUM_1
#define BODY_LINK_TX_PIN GPIO_NUM_15
#define BODY_LINK_RX_PIN GPIO_NUM_16
#define BODY_LINK_BAUD   115200
#endif

#endif
