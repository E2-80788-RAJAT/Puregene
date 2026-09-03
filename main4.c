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

// Global flags
volatile int endstop_hit_flag = 0;
volatile int program_running = 1;
int serial_fd_global = -1;

// --- Serial Setup ---
int setup_serial(const char* portname) {
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return -1;

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1; // 100ms timeout for non-blocking feeling

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

// --- THREAD 1: Serial Reader ---
// Continually reads responses from the Arduino Due
void* serial_read_thread(void* arg) {
    char buf[256];
    int idx = 0;
    while (program_running) {
        char c;
        int n = read(serial_fd_global, &c, 1);
        if (n > 0) {
            if (c == '\n') {
                buf[idx] = '\0';
                if (buf[0] == 'E') {
                    endstop_hit_flag = (buf[1] == '1') ? 1 : 0;
                } else if (buf[0] == 'T') {
                    // Overwrite the current line with live temperature data
                    printf("\r\033[K[Live Temp] H1 & H2: %s°C\n", &buf[1]);
                }
                idx = 0;
            } else {
                if (idx < 255) buf[idx++] = c;
            }
        }
    }
    return NULL;
}

// --- THREAD 2: Endstop Poller ---
// Sends "?" every 5ms to check the switch without blocking the main loop
void* endstop_poll_thread(void* arg) {
    while (program_running) {
        write(serial_fd_global, "?\n", 2);
        usleep(5000); // 5ms poll rate
    }
    return NULL;
}

// --- THREAD 3: Terminal Keyboard Listener ---
// Listens for "start heaters" and "stop heater"
void* terminal_thread(void* arg) {
    char input[256];
    while (program_running) {
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; // Strip newline
            
            if (strcmp(input, "start heaters") == 0) {
                printf(">>> Command accepted: STARTING HEATERS (Target: 65C)\n");
                write(serial_fd_global, "START\n", 6);
            } 
            else if (strcmp(input, "stop heater") == 0) {
                printf(">>> Command accepted: STOPPING HEATERS\n");
                write(serial_fd_global, "STOP\n", 5);
            }
        }
    }
    return NULL;
}

// --- Helper Function: Homing ---
void home_motor(StepperMotor* motor) {
    printf("    -> Homing motor (Direction 1)...\n");
    lgGpioWrite(motor->gpio_handle, motor->dir_pin, 1);

    // Step until the async serial thread detects the switch
    while (!endstop_hit_flag) {
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 1);
        usleep(600);
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 0);
        usleep(600);
    }
    usleep(50000); // 50ms settle time
}

// --- Main Stepper Loop ---
int main() {
    serial_fd_global = setup_serial(SERIAL_PORT);
    if (serial_fd_global < 0) {
        printf("Failed to open Serial Port.\n");
        return -1;
    }
    tcflush(serial_fd_global, TCIOFLUSH);
    sleep(2); // Wait for Arduino reset

    int handle = lgGpiochipOpen(0);
    if (handle < 0) {
        printf("Failed to open GPIO. Run with sudo.\n");
        return -1;
    }

    StepperMotor motor1;
    DRV8825_Init(&motor1, handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    DRV8825_Enable(&motor1, 1);
    usleep(50000);

    // Spin up all background threads
    pthread_t read_th, poll_th, term_th;
    pthread_create(&read_th, NULL, serial_read_thread, NULL);
    pthread_create(&poll_th, NULL, endstop_poll_thread, NULL);
    pthread_create(&term_th, NULL, terminal_thread, NULL);

    printf("System Ready. Type 'start heaters' or 'stop heater' anytime.\n");

    struct timespec target_time;
    clock_gettime(CLOCK_MONOTONIC, &target_time);

    // 10-Minute Master Loop
    for (int cycle = 1; cycle <= 60; cycle++) {
        printf("\n=== CYCLE %d === (Synchronized to 60.0s interval)\n", cycle);
        
        home_motor(&motor1);

        printf("    -> Moving to Spot 0 (Dark Current): 1000 steps Dir 0...\n");
        DRV8825_Turn(&motor1, 1000, 0, 600);
        sleep(4);

        for (int spot = 1; spot <= 6; spot++) {
            printf("    -> Moving to Spot %d: 100 steps Dir 1...\n", spot);
            DRV8825_Turn(&motor1, 100, 1, 600);
            sleep(3);
        }

        printf("    -> All 6 spots complete. Homing to conclude cycle...\n");
        home_motor(&motor1);

        target_time.tv_sec += 60;
        if (cycle < 60) {
            printf("    -> Idling at home. Waiting for exact 60s hardware boundary...\n");
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target_time, NULL);
        }
    }

    printf("\n10 Cycles Complete. Shutting down.\n");
    
    // Shut off heaters on exit for safety
    write(serial_fd_global, "STOP\n", 5);
    program_running = 0; 
    
    // Allow threads to gracefully exit
    pthread_cancel(term_th); // Cancel terminal thread blocking on stdin
    pthread_join(read_th, NULL);
    pthread_join(poll_th, NULL);

    DRV8825_Stop(&motor1);
    lgGpiochipClose(handle);
    close(serial_fd_global);

    return 0;
}
