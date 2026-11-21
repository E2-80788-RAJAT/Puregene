#include "drv8825.h"
#include <unistd.h>
#include <stdio.h>

int DRV8825_Init(StepperMotor* motor, int handle, int dir_pin, int step_pin, int enable_pin) {
    motor->gpio_handle = handle;
    motor->dir_pin = dir_pin;
    motor->step_pin = step_pin;
    motor->enable_pin = enable_pin;

    // Claim outputs and set initial states
    lgGpioClaimOutput(handle, 0, dir_pin, 0);
    lgGpioClaimOutput(handle, 0, step_pin, 0);
    lgGpioClaimOutput(handle, 0, enable_pin, 1); // Default to disabled
    return 0;
}

void DRV8825_Enable(StepperMotor* motor, int enable_logic) {
    // Newer Waveshare boards (Rev 2.1+) use 1 to enable. Older use 0.
    lgGpioWrite(motor->gpio_handle, motor->enable_pin, enable_logic);
    usleep(100); // Give chip time to wake up
}

void DRV8825_Stop(StepperMotor* motor) {
    // Disable the motor to prevent overheating
    lgGpioWrite(motor->gpio_handle, motor->enable_pin, 1);
}

void DRV8825_Turn(StepperMotor* motor, float steps, int direction, int delay_us) {
    // 1 for forward, 0 for backward
    lgGpioWrite(motor->gpio_handle, motor->dir_pin, direction);

    for(int i = 0; i < steps; i++) {
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 1);
        usleep(delay_us);
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 0);
        usleep(delay_us);
    }
}
