#include "adc1_shared.h"

#include <mutex>

adc_oneshot_unit_handle_t Adc1Shared() {
    static std::mutex mutex;
    static adc_oneshot_unit_handle_t handle = nullptr;
    std::lock_guard<std::mutex> lock(mutex);
    if (handle == nullptr) {
        adc_oneshot_unit_init_cfg_t uc = {};
        uc.unit_id = ADC_UNIT_1;
        if (adc_oneshot_new_unit(&uc, &handle) != ESP_OK) handle = nullptr;
    }
    return handle;
}
