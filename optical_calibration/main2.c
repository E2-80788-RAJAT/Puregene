
#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include "drv8825.h"

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

    // 3. Enable the motor
    // Note: The DRV8825 ENABLE pin is typically Active Low (0 = ON, 1 = OFF).
    // If your specific Waveshare HAT revision requires a 1 to enable, change this to 1.
    printf("Enabling motor...\n");
    DRV8825_Enable(&motor1, 1); 
    
    // Give it a brief moment to settle
    usleep(500000); 

    // 4. Move 200 steps in direction 0
    // The delay_us defines the speed. 1500 microseconds is a safe, moderate speed.
    printf("Moving 200 steps in direction 0...\n");
    DRV8825_Turn(&motor1, 500, 0, 600);

    // Pause for 1 second between movements
    sleep(1);

    // 5. Move 500 steps in direction 1
    printf("Moving 500 steps in direction 1...\n");
    DRV8825_Turn(&motor1, 500, 1, 600);

    // 6. Clean up and disable
    printf("Stopping motor and cleaning up...\n");
    DRV8825_Stop(&motor1); 
    lgGpiochipClose(gpio_handle);

    printf("Done!\n");
    return 0;
}
