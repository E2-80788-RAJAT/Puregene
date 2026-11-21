#ifndef DRV8825_H
#define DRV8825_H

#include <lgpio.h>

// BCM Pin Definitions for Motor 1 on Waveshare HAT
#define MOTOR1_DIR_PIN    13
#define MOTOR1_STEP_PIN   19
#define MOTOR1_ENABLE_PIN 12

// Structure to hold the motor instance
typedef struct {
    int gpio_handle;
    int dir_pin;
    int step_pin;
    int enable_pin;
} StepperMotor;

// Readymade Library Functions
int DRV8825_Init(StepperMotor* motor, int gpio_handle, int dir_pin, int step_pin, int enable_pin);
void DRV8825_Enable(StepperMotor* motor, int enable_logic);
void DRV8825_Turn(StepperMotor* motor, float steps, int direction, int delay_us);
void DRV8825_Stop(StepperMotor* motor);

#endif
