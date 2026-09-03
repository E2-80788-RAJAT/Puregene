#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include "drv8825.h"

#define LIMIT_SWITCH_PIN 24

int main() {
    // 1. Open the default GPIO chip (0 is standard for Raspberry Pi 5)
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        printf("Error: Failed to open gpiochip. Run with sudo or check lgpio installation.\n");
        return -1;
    }

    // 2. Initialize the stepper motor structure
    StepperMotor motor1;
    DRV8825_Init(&motor1, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);

    // 3. Configure the Limit Switch pin as an input
    // Using LG_SET_PULL_UP (value 32) to ensure a stable HIGH signal when not pressed
    if (lgGpioClaimInput(gpio_handle, 32, LIMIT_SWITCH_PIN) < 0) {
        printf("Error: Failed to claim limit switch pin.\n");
        lgGpiochipClose(gpio_handle);
        return -1;
    }

    // 4. Enable the motor
    printf("Enabling motor...\n");
    DRV8825_Enable(&motor1, 1); 
    usleep(500000); 

    // 5. Homing Sequence: Move in direction 0 until limit switch is hit
    printf("Homing: Moving in direction 0 until limit switch is triggered...\n");
    
    // Set direction to 0
    lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 0);
    
    // Step continuously as long as the limit switch reads HIGH (1)
    // Adjust to == 0 if your switch outputs HIGH when pressed
    while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1) {
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1);
        usleep(1500); // Step delay
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0);
        usleep(1500); // Step delay
    }
    printf("Limit switch triggered! Homing complete.\n");

    // Pause briefly after hitting the switch
    sleep(1);

    // 6. Move 500 steps in direction 0
    // Caution: If direction 0 is towards the switch, this will push into it. 
    // You may want to change the direction parameter to 1 here to back away.
    printf("Moving 500 steps in direction 0...\n");
    DRV8825_Turn(&motor1, 500, 0, 600);

    // 7. Clean up and disable
    printf("Stopping motor and cleaning up...\n");
    DRV8825_Stop(&motor1); 
    lgGpiochipClose(gpio_handle);

    printf("Done!\n");
    return 0;
}
