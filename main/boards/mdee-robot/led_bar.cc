#include "led_bar.h"
#include "config.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const gpio_num_t kLedPins[LED_BAR_COUNT] = LED_BAR_PINS;
static const unsigned kAllMask = (1u << LED_BAR_COUNT) - 1u;
static const unsigned kLeftMask = (1u << (LED_BAR_COUNT / 2)) - 1u;
static const unsigned kRightMask = kAllMask & ~kLeftMask;

LedBar& LedBar::GetInstance() {
    static LedBar inst;
    return inst;
}

void LedBar::Init() {
    if (inited_) return;
    inited_ = true;
    for (int i = 0; i < LED_BAR_COUNT; i++) {
        gpio_config_t io = {};
        io.mode = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = 1ULL << kLedPins[i];
        gpio_config(&io);
    }
    Write(0);
    xTaskCreate(&LedBar::Task, "led_bar", 2048, this, 2, nullptr);
}

void LedBar::Write(unsigned mask) {
    for (int i = 0; i < LED_BAR_COUNT; i++) {
        bool on = (mask >> i) & 1;
        gpio_set_level(kLedPins[i], LED_BAR_ACTIVE_HIGH ? on : !on);
    }
}

void LedBar::Task(void* arg) {
    auto* self = static_cast<LedBar*>(arg);
    while (true) {
        int64_t now = esp_timer_get_time() / 1000;
        unsigned mask = 0;
        switch (self->pattern_) {
            case MdeeLights::kOff: mask = 0; break;
            case MdeeLights::kOn: mask = kAllMask; break;
            case MdeeLights::kChase: {
                const int span = 2 * LED_BAR_COUNT - 2;            // bounce left-right
                int p = (now / 90) % span;
                int i = p < LED_BAR_COUNT ? p : span - p;
                mask = 1u << i;
                break;
            }
            case MdeeLights::kBlink: mask = ((now / 400) & 1) ? kAllMask : 0; break;
            case MdeeLights::kSignalLeft: mask = ((now / 350) & 1) ? kLeftMask : 0; break;
            case MdeeLights::kSignalRight: mask = ((now / 350) & 1) ? kRightMask : 0; break;
            case MdeeLights::kBatteryBar: {
                auto& robot = MdeeRobot::GetInstance();
                if (!robot.LinkUp() || robot.BatteryVolts() < 4.0f) {
                    mask = ((now / 500) & 1) ? 0x01u : 0;       // no motor-battery reading: blink one LED
                } else {
                    int n = (robot.BatteryPercent() * LED_BAR_COUNT + 99) / 100;
                    mask = n >= LED_BAR_COUNT ? kAllMask : ((1u << n) - 1u);
                }
                break;
            }
        }
        self->Write(mask);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
