/* Servo control loop — runs on core 1 */

#include "servo.h"
#include "servo_errors.h"
#include "params.h"
#include "as560x.h"
#include "tusb.h"
#include "pico/multicore.h"
#include "pico/flash.h"

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

    bool moving = false;
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
        int angle_tolerance = moving ? g_params.angle_tolerance : g_params.dead_angle;

        int pwm_a, pwm_b;
        if (angle_delta_abs > angle_tolerance) {
            gpio_put(LED_PIN, 1);
            moving = true;
            int motor_pwm = (angle_delta_abs > g_params.slow_angle) ? g_params.fast_pwm : g_params.slow_pwm;
            if (angle_delta > 0) {
                pwm_a = motor_pwm; pwm_b = 0;
            } else {
                pwm_a = 0; pwm_b = motor_pwm;
            }
        } else {
            gpio_put(LED_PIN, 0);
            pwm_a = 0; pwm_b = 0;
            moving = false;
        }
        setMotorPwm(pwm_a, pwm_b);

        g_telemetry.pwm_us = raw_pwm;
        g_telemetry.current_angle = (int16_t)angle;
        g_telemetry.target_angle = (int16_t)target_angle;
        g_telemetry.power_a = (int16_t)pwm_a;
        g_telemetry.power_b = (int16_t)pwm_b;
    }
}
