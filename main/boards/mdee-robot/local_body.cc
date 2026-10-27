#include "local_body.h"

#if CONFIG_MDEE_SOLO

#include "config.h"
#include "adc1_shared.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TAG "LocalBody"

static inline int64_t NowMs() { return esp_timer_get_time() / 1000; }
static inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

#define MOTOR_TIMER LEDC_TIMER_0
#define MOTOR_DUTY_BITS 10
#define MOTOR_MIN_START_PCT 22
#define RAMP_STEP_PCT 8
// LEDC channels: 0 = left forward (AIN1), 1 = left reverse (AIN2), 2 = right forward (BIN1), 3 = right reverse (BIN2)

LocalBody& LocalBody::GetInstance() {
    static LocalBody inst;
    return inst;
}

void LocalBody::Init() {
    if (inited_) return;
    inited_ = true;
    InitMotors();
    InitUltrasonic();
    InitNeo();
    InitBattery();
    xTaskCreate(&LocalBody::ControlTask, "solo_ctl", 4096, this, 6, nullptr);
    xTaskCreate(&LocalBody::SensorTask, "solo_sns", 3072, this, 4, nullptr);
    ESP_LOGI(TAG, "Solo body ready");
}

void LocalBody::InitMotors() {
    gpio_config_t sb = {};
    sb.mode = GPIO_MODE_OUTPUT;
    sb.pin_bit_mask = 1ULL << SOLO_MOTOR_STBY;
    gpio_config(&sb);
    gpio_set_level(SOLO_MOTOR_STBY, 0);

    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = (ledc_timer_bit_t)MOTOR_DUTY_BITS;
    t.timer_num = MOTOR_TIMER;
    t.freq_hz = 20000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    const gpio_num_t pins[4] = {SOLO_MOTOR_AIN1, SOLO_MOTOR_AIN2, SOLO_MOTOR_BIN1, SOLO_MOTOR_BIN2};
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t c = {};
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.timer_sel = MOTOR_TIMER;
        c.channel = (ledc_channel_t)i;
        c.gpio_num = pins[i];
        c.duty = 0;
        c.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }
    SetMotorRaw(0, 0);
    gpio_set_level(SOLO_MOTOR_STBY, 1);
}

void LocalBody::SetMotorRaw(int left_pct, int right_pct) {
    auto side = [](int pct, bool invert, int ch_fwd, int ch_rev) {
        if (invert) pct = -pct;
        int mag = std::abs(pct);
        uint32_t duty = 0;
        if (mag > 0) {
            int eff = MOTOR_MIN_START_PCT + (mag * (100 - MOTOR_MIN_START_PCT)) / 100;
            duty = (uint32_t)((eff * ((1 << MOTOR_DUTY_BITS) - 1)) / 100);
        }
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_fwd, pct > 0 ? duty : 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_rev, pct < 0 ? duty : 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_fwd);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_rev);
    };
    side(left_pct, SOLO_MOTOR_LEFT_INVERT, 0, 1);
    side(right_pct, SOLO_MOTOR_RIGHT_INVERT, 2, 3);
}

void LocalBody::InitUltrasonic() {
    gpio_config_t out = {};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = 1ULL << SOLO_US_TRIG_PIN;
    gpio_config(&out);
    gpio_set_level(SOLO_US_TRIG_PIN, 0);

    gpio_config_t in = {};
    in.mode = GPIO_MODE_INPUT;
    in.pull_down_en = GPIO_PULLDOWN_ENABLE;
    in.pin_bit_mask = (1ULL << SOLO_US_ECHO_FRONT_PIN) | (1ULL << SOLO_US_ECHO_REAR_PIN);
    gpio_config(&in);
}

void LocalBody::InitNeo() {
    led_strip_config_t sc = {};
    sc.strip_gpio_num = SOLO_NEOPIXEL_PIN;
    sc.max_leds = SOLO_NEOPIXEL_COUNT;
    sc.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    sc.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rc = {};
    rc.resolution_hz = 10 * 1000 * 1000;
    if (led_strip_new_rmt_device(&sc, &rc, &neo_strip_) == ESP_OK) {
        led_strip_clear(neo_strip_);
    } else {
        ESP_LOGW(TAG, "NeoPixel init failed");
        neo_strip_ = nullptr;
    }
}

void LocalBody::InitBattery() {
#if SOLO_MOTOR_BAT_MONITOR
    adc_ = Adc1Shared();
    if (adc_ == nullptr) return;
    adc_oneshot_chan_cfg_t cc = {};
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc_, SOLO_MOTOR_BAT_CHANNEL, &cc);

    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = ADC_UNIT_1;
    cal.chan = SOLO_MOTOR_BAT_CHANNEL;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cal, &adc_cali_) != ESP_OK) adc_cali_ = nullptr;
#endif
}

int LocalBody::MeasureCm(gpio_num_t echo) {
    gpio_set_level(SOLO_US_TRIG_PIN, 0);
    esp_rom_delay_us(3);
    gpio_set_level(SOLO_US_TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(SOLO_US_TRIG_PIN, 0);

    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(echo) == 0) {
        if (esp_timer_get_time() - t0 > 6000) return 400;
    }
    int64_t rise = esp_timer_get_time();
    while (gpio_get_level(echo) == 1) {
        if (esp_timer_get_time() - rise > 15000) return 400;   // ~2.5 m
    }
    int cm = (int)((esp_timer_get_time() - rise) / 58);
    return cm < 2 ? 400 : cm;
}

// ---------------------------------------------------------------- commands
void LocalBody::HandleCommand(const char* line) {
    char cmd[12] = {0};
    char word[16] = {0};
    int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(line, "%11s", cmd) != 1) return;

    if (!strcmp(cmd, "D")) {
        if (sscanf(line, "D %d %d %d", &a, &b, &c) == 3) Drive(a, b, c);
    } else if (!strcmp(cmd, "S")) {
        Stop();
    } else if (!strcmp(cmd, "MODE")) {
        MdeeMode m;
        if (sscanf(line, "MODE %15s", word) == 1 && MdeeRobot::ParseMode(word, m)) SetMode(m);
    } else if (!strcmp(cmd, "NEO")) {
        if (sscanf(line, "NEO %d %d %d %d", &a, &b, &c, &d) == 4 && a >= 0 && a <= 3) {
            neo_ = (MdeeNeo)a;
            neo_r_ = b & 255; neo_g_ = c & 255; neo_b_ = d & 255;
        }
    }
    // SV / EAR / FID / LT / PING: nothing to do in the solo build (no servos; LED bar is driven by LedBar)
}

void LocalBody::Drive(int left_pct, int right_pct, int hold_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = MdeeMode::kManual;
    cmd_left_ = Clamp(left_pct, -100, 100);
    cmd_right_ = Clamp(right_pct, -100, 100);
    drive_deadline_ms_ = NowMs() + (hold_ms > 0 ? Clamp(hold_ms, 50, 60000) : 60000);
}

void LocalBody::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = MdeeMode::kManual;
    cmd_left_ = cmd_right_ = 0;
    drive_deadline_ms_ = 0;
}

void LocalBody::SetMode(MdeeMode m) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = m;
    cmd_left_ = cmd_right_ = 0;
    drive_deadline_ms_ = 0;
    wander_state_ = W::kCruise;
    avoid_count_ = 0;
}

// ---------------------------------------------------------------- tasks
void LocalBody::SensorTask(void* arg) {
    auto* self = static_cast<LocalBody*>(arg);
    int fbuf[3] = {400, 400, 400}, rbuf[3] = {400, 400, 400};
    int idx = 0, low_count = 0, loops = 0;
    while (true) {
        fbuf[idx] = self->MeasureCm(SOLO_US_ECHO_FRONT_PIN);
        vTaskDelay(pdMS_TO_TICKS(35));
        rbuf[idx] = self->MeasureCm(SOLO_US_ECHO_REAR_PIN);
        idx = (idx + 1) % 3;
        auto median = [](int* b) {
            int a[3] = {b[0], b[1], b[2]};
            std::sort(a, a + 3);
            return a[1];
        };
        self->front_cm_ = median(fbuf);
        self->rear_cm_ = median(rbuf);
        vTaskDelay(pdMS_TO_TICKS(35));

#if SOLO_MOTOR_BAT_MONITOR
        if (self->adc_ != nullptr && ++loops % 7 == 0) {            // about every 0.5 s
            long sum = 0;
            int n = 0;
            for (int i = 0; i < 16; i++) {
                int raw = 0;
                if (adc_oneshot_read(self->adc_, SOLO_MOTOR_BAT_CHANNEL, &raw) == ESP_OK) { sum += raw; n++; }
            }
            if (n > 0) {
                int raw = (int)(sum / n), mv = 0;
                if (!(self->adc_cali_ && adc_cali_raw_to_voltage(self->adc_cali_, raw, &mv) == ESP_OK)) mv = raw * 3100 / 4095;
                float v = (mv / 1000.0f) * (SOLO_MOTOR_BAT_R_UPPER_OHM + SOLO_MOTOR_BAT_R_LOWER_OHM) /
                          SOLO_MOTOR_BAT_R_LOWER_OHM * SOLO_MOTOR_BAT_CAL_FACTOR;
                float old = self->battery_v_;
                self->battery_v_ = (old > 4.0f && v > 4.0f) ? (old * 0.8f + v * 0.2f) : v;
                // below 5 V = no pack connected (USB only / divider not wired): never cut off
                if (v > 5.0f && v < SOLO_MOTOR_BAT_EMPTY_V - 0.3f) low_count++; else low_count = 0;
                self->battery_cutoff_ = SOLO_MOTOR_BAT_CUTOFF && low_count >= 10;
            }
        }
#else
        (void)loops; (void)low_count;
#endif
    }
}

void LocalBody::ControlTask(void* arg) {
    auto* self = static_cast<LocalBody*>(arg);
    while (true) {
        self->ControlStep();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void LocalBody::ControlStep() {
    int64_t now = NowMs();
    int tl, tr;
    MdeeMode mode;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mode_ == MdeeMode::kWander) WanderStep(now);
        else if (mode_ == MdeeMode::kFollow) FollowStep();
        else if (drive_deadline_ms_ != 0 && now > drive_deadline_ms_) {   // deadman
            cmd_left_ = cmd_right_ = 0;
            drive_deadline_ms_ = 0;
        }
        tl = cmd_left_;
        tr = cmd_right_;
        mode = mode_;
    }

    // ---- safety layer ----
    int front = front_cm_, rear = rear_cm_;
    bool pivot = (tl * tr) < 0;
    if (!pivot) {
        if (tl + tr > 0) {
            if (front < SOLO_OBSTACLE_STOP_CM) { tl = tr = 0; }
            else if (front < SOLO_OBSTACLE_SLOW_CM) {
                int pct = 40 + 60 * (front - SOLO_OBSTACLE_STOP_CM) / (SOLO_OBSTACLE_SLOW_CM - SOLO_OBSTACLE_STOP_CM);
                tl = tl * pct / 100; tr = tr * pct / 100;
            }
        } else if (tl + tr < 0) {
            if (rear < SOLO_OBSTACLE_STOP_CM) { tl = tr = 0; }
            else if (rear < SOLO_OBSTACLE_SLOW_CM) {
                int pct = 40 + 60 * (rear - SOLO_OBSTACLE_STOP_CM) / (SOLO_OBSTACLE_SLOW_CM - SOLO_OBSTACLE_STOP_CM);
                tl = tl * pct / 100; tr = tr * pct / 100;
            }
        }
    }
    if (battery_cutoff_) tl = tr = 0;

    // ---- ramp (instant when stopping) ----
    auto ramp = [](int cur, int target) {
        if (target == 0) return 0;
        if (target > cur) return std::min(cur + RAMP_STEP_PCT, target);
        return std::max(cur - RAMP_STEP_PCT, target);
    };
    int nl = ramp(out_left_, tl), nr = ramp(out_right_, tr);
    if (nl != out_left_ || nr != out_right_) {
        out_left_ = nl;
        out_right_ = nr;
        SetMotorRaw(out_left_, out_right_);
    }

    NeoStep(now);

    // ---- tell the rest of the firmware (same data the UART body would send) ----
    if (++tick_ % 5 == 0) {
        float v = battery_v_;
        int pct = (int)((v - SOLO_MOTOR_BAT_EMPTY_V) * 100.0f / (SOLO_MOTOR_BAT_FULL_V - SOLO_MOTOR_BAT_EMPTY_V));
        pct = Clamp(pct, 0, 100);
        MdeeRobot::GetInstance().UpdateStatus(front_cm_, rear_cm_, (int)(v * 1000.0f), pct,
                                              battery_cutoff_, out_left_, out_right_, (int)mode);
    }
}

void LocalBody::WanderStep(int64_t now) {
    const int cruise = MdeeRobot::SpeedPct(MdeeSpeed::kMedium);
    int front = front_cm_, rear = rear_cm_;
    switch (wander_state_) {
        case W::kCruise:
            cmd_left_ = cmd_right_ = cruise;
            if (front < SOLO_OBSTACLE_STOP_CM + 6) {
                if (now - avoid_window_start_ms_ > 6000) { avoid_window_start_ms_ = now; avoid_count_ = 0; }
                avoid_count_++;
                wander_turn_dir_ = (esp_random() & 1) ? 1 : -1;
                if (rear > SOLO_OBSTACLE_STOP_CM + 10) {
                    wander_state_ = W::kBackup;
                    wander_until_ms_ = now + (avoid_count_ >= 3 ? 900 : 450);
                } else {
                    wander_state_ = W::kTurn;
                    wander_until_ms_ = now + 500 + (esp_random() % 500);
                }
            }
            break;
        case W::kBackup:
            cmd_left_ = cmd_right_ = -MdeeRobot::SpeedPct(MdeeSpeed::kSlow);
            if (now > wander_until_ms_ || rear < SOLO_OBSTACLE_STOP_CM + 4) {
                wander_state_ = W::kTurn;
                wander_until_ms_ = now + 450 + (esp_random() % 650) + (avoid_count_ >= 3 ? 500 : 0);
            }
            break;
        case W::kTurn: {
            int s = MdeeRobot::SpeedPct(MdeeSpeed::kMedium);
            cmd_left_ = -s * wander_turn_dir_;
            cmd_right_ = s * wander_turn_dir_;
            if (now > wander_until_ms_) wander_state_ = W::kCruise;
            break;
        }
    }
}

void LocalBody::FollowStep() {
    int d = front_cm_;
    int slow = MdeeRobot::SpeedPct(MdeeSpeed::kSlow), med = MdeeRobot::SpeedPct(MdeeSpeed::kMedium);
    if (d >= 90) { cmd_left_ = cmd_right_ = 0; }
    else if (d > 40) { int s = d > 65 ? med : slow; cmd_left_ = cmd_right_ = s; }
    else if (d < 22 && rear_cm_ > SOLO_OBSTACLE_STOP_CM + 8) { cmd_left_ = cmd_right_ = -slow; }
    else { cmd_left_ = cmd_right_ = 0; }
}

void LocalBody::NeoStep(int64_t now) {
    static int64_t last = 0;
    if (neo_strip_ == nullptr || now - last < 40) return;
    last = now;
    switch (neo_) {
        case MdeeNeo::kOff: led_strip_clear(neo_strip_); break;
        case MdeeNeo::kSolid:
            for (int i = 0; i < SOLO_NEOPIXEL_COUNT; i++) led_strip_set_pixel(neo_strip_, i, neo_r_, neo_g_, neo_b_);
            led_strip_refresh(neo_strip_);
            break;
        case MdeeNeo::kBreathe: {
            float k = 0.15f + 0.85f * (0.5f + 0.5f * sinf(now / 450.0f));
            for (int i = 0; i < SOLO_NEOPIXEL_COUNT; i++)
                led_strip_set_pixel(neo_strip_, i, neo_r_ * k, neo_g_ * k, neo_b_ * k);
            led_strip_refresh(neo_strip_);
            break;
        }
        case MdeeNeo::kRainbow:
            for (int i = 0; i < SOLO_NEOPIXEL_COUNT; i++)
                led_strip_set_pixel_hsv(neo_strip_, i, (uint16_t)((now / 8 + i * 60) % 360), 255, 60);
            led_strip_refresh(neo_strip_);
            break;
    }
}

#endif  // CONFIG_MDEE_SOLO
