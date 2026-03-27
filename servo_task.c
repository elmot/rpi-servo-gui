/* Servo control loop — runs on core 1 */

#include "servo.h"
#include "servo_errors.h"
#include "params.h"
#include "as560x.h"
#include "tusb.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/time.h"

typedef enum {
    MOTOR_STOPPED,
    MOTOR_RAMPING,  /* starting: linear ramp from cutoff to target speed */
    MOTOR_RUNNING,  /* at full requested speed */
    MOTOR_PAUSING   /* direction change: motor off for slow_start_ms */
} motor_state_t;

_Noreturn void magnetError(void);

_Noreturn static inline void error(uint32_t error_code, int phase_on_ms, int phase_off_ms) {
    setMotorPwm(0, 0);
    multicore_fifo_push_blocking(error_code);
    watchdog_enable(5000, 1);
    while (1) {
        if (tud_connected())
        {
            watchdog_update();
        }
            gpio_put(LED_PIN, 1);
            sleep_ms(phase_on_ms);
            gpio_put(LED_PIN, 0);
            sleep_ms(phase_off_ms);

    }
}

_Noreturn void i2cError(void) {
    error(SERVO_ERR_I2C, 300, 30);
}

_Noreturn void magnetError(void) {
    error(SERVO_ERR_MAGNET, 700, 300);
}

void servo_core1_entry(void) {
    flash_safe_execute_core_init();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    init_pwm();

    as560x_init();
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    motor_state_t motor_state = MOTOR_STOPPED;
    int motor_direction = 0;  /* +1 or -1 */
    uint32_t ramp_start = 0;
    uint32_t ramp_end = 0;
    while (1) {
        uint8_t status = (uint8_t)as560xGetStatus();
        if (!(status & AS560x_STATUS_MAGNET_DETECTED)) {
            magnetError();
        }
        uint16_t raw_pwm = g_params.pwm_mock ? g_params.pwm_mock : pwm_count;
        if (raw_pwm == 0) continue;

#define PWM_LOW_LIMIT  (1450)
#define PWM_HIGH_LIMIT (1550)
        uint16_t pwm = raw_pwm;
        if (pwm < PWM_LOW_LIMIT) pwm = PWM_LOW_LIMIT;
        else if (pwm > PWM_HIGH_LIMIT) pwm = PWM_HIGH_LIMIT;

        int zra = g_params.zero_restricted_angle;
        int target_angle = zra +
            ((pwm - PWM_LOW_LIMIT) * (360 - 2 * zra)) /
            (PWM_HIGH_LIMIT - PWM_LOW_LIMIT);

        int angle = as560xReadAngle() * 360L / AS5601_ANGLE_MAX;
        if (g_params.angle_reversed) angle = 360 - angle;
        int angle_delta = target_angle - angle;
        int angle_delta_abs = abs(angle_delta);
        bool is_active = (motor_state != MOTOR_STOPPED);
        int angle_tolerance = is_active ? g_params.angle_tolerance : g_params.dead_angle;

        int pwm_a, pwm_b;
        if (angle_delta_abs > angle_tolerance) {
            gpio_put(LED_PIN, 1);
            int wanted_dir = (angle_delta > 0) ? 1 : -1;
            int target_speed = (angle_delta_abs > g_params.slow_angle) ? g_params.fast_pwm : g_params.slow_pwm;
            uint32_t now = to_ms_since_boot(get_absolute_time());

            if (motor_state == MOTOR_PAUSING) {
                /* Waiting for direction-change pause to finish */
                if (now >= ramp_end) {
                    /* Pause done — start ramping in new direction */
                    motor_direction = wanted_dir;
                    motor_state = MOTOR_RAMPING;
                    ramp_start = now;
                    ramp_end = now + g_params.slow_start_ms;
                }
                pwm_a = 0; pwm_b = 0;

            } else if (is_active && wanted_dir != motor_direction) {
                /* Direction change — pause first */
                motor_state = MOTOR_PAUSING;
                ramp_start = now;
                ramp_end = now + g_params.slow_start_ms;
                pwm_a = 0; pwm_b = 0;

            } else if (motor_state == MOTOR_STOPPED) {
                /* Start from stop — begin ramping */
                motor_direction = wanted_dir;
                motor_state = MOTOR_RAMPING;
                ramp_start = now;
                ramp_end = now + g_params.slow_start_ms;
                /* First iteration: use cutoff_pwm */
                int motor_pwm = g_params.cutoff_pwm;
                if (motor_direction > 0) { pwm_a = motor_pwm; pwm_b = 0; }
                else                     { pwm_a = 0; pwm_b = motor_pwm; }

            } else if (motor_state == MOTOR_RAMPING) {
                /* Linear ramp: cutoff_pwm → target_speed over slow_start_ms */
                uint32_t elapsed = now - ramp_start;
                uint32_t duration = ramp_end - ramp_start;
                int motor_pwm;
                if (elapsed >= duration || duration == 0) {
                    motor_pwm = target_speed;
                    motor_state = MOTOR_RUNNING;
                } else {
                    int lo = g_params.cutoff_pwm;
                    motor_pwm = lo + (target_speed - lo) * (int)elapsed / (int)duration;
                }
                if (motor_direction > 0) { pwm_a = motor_pwm; pwm_b = 0; }
                else                     { pwm_a = 0; pwm_b = motor_pwm; }

            } else {
                /* MOTOR_RUNNING — full requested speed */
                motor_direction = wanted_dir;
                int motor_pwm = target_speed;
                if (motor_direction > 0) { pwm_a = motor_pwm; pwm_b = 0; }
                else                     { pwm_a = 0; pwm_b = motor_pwm; }
            }
        } else {
            gpio_put(LED_PIN, 0);
            pwm_a = 0; pwm_b = 0;
            motor_state = MOTOR_STOPPED;
        }
        setMotorPwm(pwm_a, pwm_b);

        g_telemetry.pwm_us = raw_pwm;
        g_telemetry.current_angle = (int16_t)angle;
        g_telemetry.target_angle = (int16_t)target_angle;
        g_telemetry.power_a = (int16_t)pwm_a;
        g_telemetry.power_b = (int16_t)pwm_b;
    }
}
