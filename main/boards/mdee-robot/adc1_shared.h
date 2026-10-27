#ifndef ADC1_SHARED_H
#define ADC1_SHARED_H
#include <esp_adc/adc_oneshot.h>

// ADC1 can be opened only once. The logic-battery meter (GPIO1) and, in the single-board build,
// the motor-battery meter (GPIO2) both use this one shared unit (different channels).
adc_oneshot_unit_handle_t Adc1Shared();

#endif
