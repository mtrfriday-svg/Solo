#ifndef LED_BAR_H
#define LED_BAR_H

#include "mdee_robot.h"   // MdeeLights

// 8 white LEDs wired straight to brain GPIOs. Same patterns as the body's PCF8574 version:
// off, on, chase, blink, left/right turn signal, battery bar (motor-pack charge).
class LedBar {
public:
    static LedBar& GetInstance();
    void Init();
    void Set(MdeeLights pattern) { pattern_ = pattern; }

private:
    LedBar() = default;
    static void Task(void* arg);
    void Write(unsigned mask);

    volatile MdeeLights pattern_ = MdeeLights::kOff;
    bool inited_ = false;
};

#endif
