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
// Continually checks the switch. If the motor backs away, the flag automatically resets to 0.
void* endstop_monitor(void* arg) {
    while (program_running) {
        if (is_endstop_hit(serial_fd_global)) {
            endstop_hit_flag = 1;
        } else {
            endstop_hit_flag = 0; 
        }
        usleep(2000); // 2ms polling rate
    }
    return NULL;
}

// --- Helper Function: Homing ---
void home_motor(StepperMotor* motor) {
    printf("    -> Homing motor (Direction 1)...\n");
    lgGpioWrite(motor->gpio_handle, motor->dir_pin, 1); 
    
    // Step until the background thread detects the switch
    while (!endstop_hit_flag) {
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 1);
        usleep(600);
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 0);
        usleep(600);
    }
    usleep(50000); // 50ms pause to let mechanical momentum settle
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
    // INITIALIZATION: Grab the exact starting hardware clock time
    // ==========================================================
    struct timespec target_time;
    clock_gettime(CLOCK_MONOTONIC, &target_time);

    // ==========================================================
    // THE 10-MINUTE MASTER LOOP
    // ==========================================================
    for (int cycle = 1; cycle <= 60; cycle++) {
        printf("\n======================================================\n");
        printf("=== CYCLE %d === (Synchronized to 60.0s interval)\n", cycle);
        printf("======================================================\n");
        
        // 1. Always ensure we start from a known zero point
        home_motor(&motor1);

        // 2. Move to Spot 0 (Dark Current)
        printf("    -> Moving to Spot 0 (Dark Current): 1000 steps Dir 0...\n");
        DRV8825_Turn(&motor1, 1000, 0, 600);
        printf("    [Spot 0] Arrived. Waiting 4 seconds...\n");
        sleep(4);

        // 3. Process the 6 Scanning Spots
        for (int spot = 1; spot <= 6; spot++) {
            printf("    -> Moving to Spot %d: 100 steps Dir 1...\n", spot);
            DRV8825_Turn(&motor1, 100, 1, 600);
            
            printf("    [Spot %d] Arrived. Waiting 3 seconds...\n", spot);
            sleep(3); 
        }

        printf("    -> All 6 spots complete. Homing to conclude cycle...\n");
        
        // 4. Return to home to finish the active part of the cycle
        home_motor(&motor1);

        // 5. Time Synchronization for the NEXT cycle
        // Update the target time by adding exactly 60 seconds
        target_time.tv_sec += 60;

        if (cycle < 10) {
            printf("    -> Idling at home. Waiting for exact 60s hardware boundary...\n");
            // Put the CPU to sleep until the exact millisecond of the target time
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
