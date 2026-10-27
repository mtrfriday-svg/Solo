#ifndef LOCAL_BODY_H
#define LOCAL_BODY_H

#include <sdkconfig.h>

#if CONFIG_MDEE_SOLO
// Body hardware on the SAME board (single-board test build): TB6612 motors, 2 ultrasonic sensors,
// 2 NeoPixels, motor-battery meter, safety, wander/follow. Takes the same text commands that the
// body board takes over UART ("D l r hold", "S", "MODE x", "NEO e r g b"); servos/ears/PCF commands are ignored.
#include "mdee_robot.h"

#include <led_strip.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <atomic>
#include <mutex>

class LocalBody {
public:
    static LocalBody& GetInstance();
    void Init();
    void HandleCommand(const char* line);

private:
    LocalBody() = default;
    void InitMotors();
    void InitUltrasonic();
    void InitNeo();
    void InitBattery();
    void SetMotorRaw(int left_pct, int right_pct);
    int MeasureCm(gpio_num_t echo);
    void Drive(int left_pct, int right_pct, int hold_ms);
    void Stop();
    void SetMode(MdeeMode m);

    static void ControlTask(void* arg);   // 20 ms: deadman, safety, autonomy, lights, status
    static void SensorTask(void* arg);    // ultrasonic + motor battery
    void ControlStep();
    void WanderStep(int64_t now_ms);
    void FollowStep();
    void NeoStep(int64_t now_ms);

    std::mutex mutex_;
    std::atomic<int> front_cm_{400};
    std::atomic<int> rear_cm_{400};
    std::atomic<float> battery_v_{0.0f};
    std::atomic<bool> battery_cutoff_{false};

    int cmd_left_ = 0, cmd_right_ = 0;     // requested (percent)
    int out_left_ = 0, out_right_ = 0;     // applied after safety/ramp
    int64_t drive_deadline_ms_ = 0;
    MdeeMode mode_ = MdeeMode::kManual;

    enum class W { kCruise, kBackup, kTurn } wander_state_ = W::kCruise;
    int64_t wander_until_ms_ = 0;
    int wander_turn_dir_ = 1;
    int avoid_count_ = 0;
    int64_t avoid_window_start_ms_ = 0;

    MdeeNeo neo_ = MdeeNeo::kSolid;
    uint8_t neo_r_ = 0, neo_g_ = 40, neo_b_ = 0;
    led_strip_handle_t neo_strip_ = nullptr;

    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t adc_cali_ = nullptr;
    int tick_ = 0;
    bool inited_ = false;
};

#endif  // CONFIG_MDEE_SOLO
#endif
