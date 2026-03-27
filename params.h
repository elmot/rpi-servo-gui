#ifndef RPI_SERVO_GUI_PARAMS_H
#define RPI_SERVO_GUI_PARAMS_H

#include <stdint.h>
#include <stdbool.h>

#ifndef SERVO_VERSION
#define SERVO_VERSION "0.1"
#endif

typedef struct {
    /* Angle parameters (degrees) */
    uint16_t zero_restricted_angle;
    uint16_t angle_tolerance;
    uint16_t dead_angle;
    uint16_t slow_angle;
    uint16_t angle_reversed;
    /* PWM parameters (percent 0–100) */
    uint16_t cutoff_pwm;
    uint16_t slow_pwm;
    uint16_t fast_pwm;
    /* Timing (ms) */
    uint16_t slow_start_ms;
    /* PWM input range (microseconds) */
    uint16_t pwm_low_limit;
    uint16_t pwm_high_limit;
    /* Debug: 0 = use real PWM input, nonzero = mock value in microseconds */
    uint16_t pwm_mock;
} servo_params_t;

#define PARAMS_DEFAULTS { \
    .zero_restricted_angle = 5, \
    .angle_tolerance = 2, \
    .dead_angle = 5, \
    .slow_angle = 20, \
    .angle_reversed = 0, \
    .cutoff_pwm = 50, \
    .slow_pwm = 70, \
    .fast_pwm = 100, \
    .slow_start_ms = 200, \
    .pwm_low_limit = 1200, \
    .pwm_high_limit = 1700, \
    .pwm_mock = 0, \
}

/* Global runtime params — written by core 0, read by core 1. */
extern volatile servo_params_t g_params;

/* Serialize params to "key=value\n" text. Returns length written (excl. NUL). */
int params_serialize(char *buf, int maxlen);

/* Parse "key=value\n" text and update g_params. Unknown keys are ignored.
 * Returns true if at least one param was updated. */
bool params_deserialize(const char *buf, int len);

void params_load_from_flash(void);
bool params_save_to_flash(void);

/* Telemetry — written by core 1, read by core 0 */
typedef struct {
    volatile uint16_t pwm_us;
    volatile int16_t  current_angle;
    volatile int16_t  target_angle;
    volatile int16_t  power_a;
    volatile int16_t  power_b;
} servo_telemetry_t;

extern servo_telemetry_t g_telemetry;

#endif
