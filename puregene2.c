#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lgpio.h>
#include "drv8825.h"

// Limit switches
#define LIMIT_SWITCH_PIN_TRAY 5
#define LIMIT_SWITCH_PIN_OPTICAL 6

// Motor 1 Pins (Tray) - Assuming your original pin definitions
#define MOTOR1_DIR_PIN 17
#define MOTOR1_STEP_PIN 27
#define MOTOR1_ENABLE_PIN 22

// Motor 2 Pins (Optical) - UPDATE THESE WITH YOUR ACTUAL GPIO PINS
#define MOTOR2_DIR_PIN 23 
#define MOTOR2_STEP_PIN 24
#define MOTOR2_ENABLE_PIN 25

// Helper function to remove trailing newlines from standard input
void clear_newline(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}

int main(void) {
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        printf("Failed to open gpiochip\n");
        return -1;
    }

    StepperMotor motor1;
    StepperMotor motor2;

    // Initialize both motors
    DRV8825_Init(&motor1, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    DRV8825_Init(&motor2, gpio_handle, MOTOR2_DIR_PIN, MOTOR2_STEP_PIN, MOTOR2_ENABLE_PIN);
    
    // Setup limit switches
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN_TRAY);
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN_OPTICAL);
    
    // Enable both motors
    DRV8825_Enable(&motor1, 1);
    DRV8825_Enable(&motor2, 1);
    usleep(500000);

    char command[256];

    printf("System Ready. Waiting for serial commands...\n");

    while(1) {
        printf("\nEnter command (tray in, tray out, start optical) or 'exit' to quit: ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Strip newline character typed by user
        clear_newline(command);

        if (strcmp(command, "tray in") == 0) {
            printf("Homing tray (motor 1)...\n");
            lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 0); // Direction 0
            
            while(lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN_TRAY) == 1) {
                lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1);
                usleep(600);
                lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0);
                usleep(600);
            }
            printf("Tray in complete.\n");
        } 
        else if (strcmp(command, "tray out") == 0) {
            printf("Moving tray out (motor 1)...\n");
            // Moves the previously hardcoded 3150 steps in direction 1
            DRV8825_Turn(&motor1, 3150, 1, 600); 
            printf("Tray out complete.\n");
        } 
        else if (strcmp(command, "start optical") == 0) {
            printf("Homing optical reader (motor 2)...\n");
            lgGpioWrite(gpio_handle, MOTOR2_DIR_PIN, 0); // Direction 0
            
            // 1. Move until it hits the limit switch
            while(lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN_OPTICAL) == 1) {
                lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 1);
                usleep(600);
                lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 0);
                usleep(600);
            }
            
            // Short stabilization sleep after hitting the switch
            sleep(1); 
            
            // 2. Move 100 steps in direction 1 to reach Spot 1
            printf("Limit reached. Moving to Spot 1...\n");
            DRV8825_Turn(&motor2, 100, 1, 600);
            printf("Reached Spot 1.\n");

            // 3. Move to spots 2, 3, 4, 5, and 6 with a 1-minute interval
            for (int spot = 2; spot <= 6; spot++) {
                printf("Waiting 1 minute for Spot %d...\n", spot);
                sleep(60); // Halts execution for exactly 60 seconds
                
                printf("Moving to Spot %d...\n", spot);
                DRV8825_Turn(&motor2, 200, 1, 600); // 200 steps to the next spot
                printf("Reached Spot %d.\n", spot);
            }
            printf("Optical reading sequence complete.\n");
        }
        else if (strcmp(command, "exit") == 0) {
            printf("Exiting program...\n");
            break;
        }
        else {
            printf("Unknown command. Please try again.\n");
        }
    }

    // Cleanup
    DRV8825_Enable(&motor1, 0);
    DRV8825_Enable(&motor2, 0);
    lgGpiochipClose(gpio_handle);
    
    return 0;
}
