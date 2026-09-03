#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include <lgpio.h>
#include "drv8825.h"

#define SERIAL_PORT "/dev/ttyACM0"

// Global flags for thread communication
volatile int endstop_hit_flag = 0;
volatile int program_running = 1;
int serial_fd_global = -1;

// --- Serial Setup and Polling Functions ---

int setup_serial(const char* portname) {
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return -1;

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; 
    tty.c_cflag |= (CLOCAL | CREAD); 
    tty.c_cflag &= ~(PARENB | PARODD); 
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0; 
    tty.c_cc[VTIME] = 1; 

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

int is_endstop_hit(int serial_fd) {
    write(serial_fd, "?", 1);
    usleep(1000); 
    char buf[8];
    int n = read(serial_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        if (strchr(buf, '1') != NULL) return 1;
    }
    return 0; 
}

// --- Background Thread Function ---
void* endstop_monitor(void* arg) {
    while (program_running) {
        if (is_endstop_hit(serial_fd_global)) {
            endstop_hit_flag = 1;
        } else {
            // Automatically clear flag when motor backs away from the switch
            endstop_hit_flag = 0; 
        }
        usleep(2000); // 2ms polling rate
    }
    return NULL;
}

// --- Helper Function: Homing ---
void home_motor(StepperMotor* motor) {
    printf("    Homing motor...\n");
    lgGpioWrite(motor->gpio_handle, motor->dir_pin, 1); // Direction 1 (towards switch)
    
    while (!endstop_hit_flag) {
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 1);
        usleep(600);
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 0);
        usleep(600);
    }
    usleep(20000); // Let mechanical momentum settle after hitting switch
}

// --- Main Program ---

int main() {
    // 1. Setup USB Serial to ATmega
    int serial_fd = setup_serial(SERIAL_PORT);
    if (serial_fd < 0) {
        printf("Failed to open Serial Port. Check connection.\n");
        return -1;
    }
    serial_fd_global = serial_fd;
    tcflush(serial_fd, TCIOFLUSH);
    sleep(2); // Wait for Arduino to reset

    // 2. Setup GPIO for Stepper Motor
    int handle = lgGpiochipOpen(0);
    if (handle < 0) {
        printf("Failed to open GPIO. Run with sudo.\n");
        return -1;
    }

    StepperMotor motor1;
    DRV8825_Init(&motor1, handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    DRV8825_Enable(&motor1, 1);
    usleep(50000); 

    // 3. Start the background monitoring thread
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, endstop_monitor, NULL);

    // ==========================================================
    // INITIALIZATION: Get to the very first Spot 1
    // ==========================================================
    printf("Starting Initial Sequence...\n");
    home_motor(&motor1);
   // printf("    Moving to Spot 1...\n");
   // DRV8825_Turn(&motor1, 1200, 0, 600); // 500 steps direction 0
   // DRV8825_Turn(&motor1, 100, 1, 600);  // 50 steps direction 1 (Arrive at Spot 1)

    // Log the exact hardware time we arrived at Spot 1 for the first time
    struct timespec target_time;
    clock_gettime(CLOCK_MONOTONIC, &target_time);

    // ==========================================================
    // THE 10-MINUTE LOOP
    // ==========================================================
    for (int cycle = 1; cycle <= 60; cycle++) {
	DRV8825_Turn(&motor1, 1000, 0, 600);
    	sleep(2);	
        printf("\n=== CYCLE %d === (Synchronized to exactly 60.0s interval)\n", cycle);
        
        // Process the 6 Spots
        for (int spot = 1; spot <= 7; spot++) {

            printf("    Executing Spot %d: Waiting 1 second...\n", spot);
            sleep(4); 
            
            if (spot < 7) {
                // Move 50 steps in direction 1 to the next spot;
                DRV8825_Turn(&motor1, 100, 1, 1000);
            }
        }

        printf("    All 6 spots complete.\n");
        home_motor(&motor1);

        // Update the target time for the NEXT cycle (+60 seconds exactly)
        target_time.tv_sec += 60;

        // If we are not on the last cycle, wait and prepare for the next round
        if (cycle < 10) {
            printf("    Waiting at home position for next cycle...\n");

            // 1. Calculate a wake-up time 1 second BEFORE the target time
            struct timespec wakeup_time = target_time;
            wakeup_time.tv_sec -= 1;

            // 2. Put the CPU to sleep until exactly 1 second remains
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);

            // 3. Drive out to Spot 1 (This takes approx 0.66 seconds)
            DRV8825_Turn(&motor1, 500, 0, 600);
            DRV8825_Turn(&motor1, 50, 1, 600);

            // 4. We arrived a fraction of a second early. Lock onto the exact target millisecond.
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target_time, NULL);
        }
    }

    // ==========================================================
    // CLEANUP
    // ==========================================================
    printf("\n10 Cycles Complete. Shutting down.\n");
    program_running = 0; // Signal thread to close
    pthread_join(monitor_thread, NULL); 
    
    DRV8825_Stop(&motor1);
    lgGpiochipClose(handle);
    close(serial_fd);

    return 0;
}
