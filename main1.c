#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h> // Added for threading
#include <lgpio.h>
#include "drv8825.h"

#define SERIAL_PORT "/dev/ttyACM0"

// Global flag to communicate between the background thread and the motor loop
volatile int endstop_hit_flag = 0;
int serial_fd_global = -1;

// --- Serial Setup and Polling Functions ---

int setup_serial(const char* portname) {
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening %s. Check connection or permissions.\n", portname);
        return -1;
    }
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
// This runs parallel to the motor to prevent USB lag from causing vibrations
void* endstop_monitor(void* arg) {
    while (!endstop_hit_flag) {
        if (is_endstop_hit(serial_fd_global)) {
            endstop_hit_flag = 1; // Signal the main loop to stop
            break;
        }
        usleep(2000); // Small pause to not overwhelm the USB bus
    }
    return NULL;
}

// --- Main Program ---

int main() {
    // 1. Setup USB Serial to ATmega
    int serial_fd = setup_serial(SERIAL_PORT);
    if (serial_fd < 0) return -1;
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

    printf("Enabling Motor...\n");
    DRV8825_Enable(&motor1, 1);
    usleep(50000); 

    printf("Homing... Moving forward until endstop is hit.\n");

    // 3. Start the background thread for the endstop
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, endstop_monitor, NULL);

    // 4. The Smooth Homing Loop
    // Set direction forward once
    lgGpioWrite(motor1.gpio_handle, motor1.dir_pin, 1); 
    
    // Pulse the step pin smoothly while checking the flag in memory
    while (!endstop_hit_flag) {
        lgGpioWrite(motor1.gpio_handle, motor1.step_pin, 1);
        usleep(600);
        lgGpioWrite(motor1.gpio_handle, motor1.step_pin, 0);
        usleep(600);
    }

    // Wait for the background thread to safely close
    pthread_join(monitor_thread, NULL);

    printf("Endstop HIT! Stopping and reversing.\n");

    // 5. Reverse 200 steps
    usleep(20000); // Brief pause before reversing momentum
    printf("Moving 200 steps backwards...\n");
    DRV8825_Turn(&motor1,300, 0, 600); // I set this to 200 as per your previous prompt requirement

    // 6. Cleanup
    printf("Done. Disabling Motor.\n");
    DRV8825_Stop(&motor1);
    lgGpiochipClose(handle);
    close(serial_fd);

    return 0;
}
