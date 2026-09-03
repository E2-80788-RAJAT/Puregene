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

// --- Hardware Definitions ---
#define SERIAL_PORT "/dev/ttyACM0"
#define PIN_12V_EN 17 
#define PIN_LED_EN 27 

#define ADS1115_ADDR 0x48
#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

// UPDATE THESE PINS FOR MOTOR 1
#define MOTOR1_DIR_PIN 13
#define MOTOR1_STEP_PIN 19
#define MOTOR1_ENABLE_PIN 12

// UPDATE THESE PINS FOR MOTOR 2 (WAVESHARE HAT TRAY)
#define MOTOR2_DIR_PIN 24
#define MOTOR2_STEP_PIN 18
#define MOTOR2_ENABLE_PIN 4

// --- Global Flags & Data ---
volatile int endstop1_hit_flag = 0;
volatile int endstop2_hit_flag = 0;
volatile int program_running = 1;
volatile int start_optical_flag = 0;
int serial_fd_global = -1;
int gpio_handle_global = -1;

StepperMotor motor1;
StepperMotor motor2;

volatile float global_temp1 = 0.0;
volatile float global_temp2 = 0.0;
volatile float global_temp3 = 0.0; 

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
    tty.c_cc[VTIME] = 1; 

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

// --- THREAD 1: Serial Reader ---
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
                    // Arduino sends: "E1,0" -> Motor 1 (buf[1]), Motor 2 (buf[3])
                    endstop1_hit_flag = (buf[1] == '1') ? 1 : 0;
                    endstop2_hit_flag = (buf[3] == '1') ? 1 : 0;
                } else if (buf[0] == 'T') {
                    sscanf(&buf[1], "%f,%f,%f", (float*)&global_temp1, (float*)&global_temp2, (float*)&global_temp3);
                    printf("\r\033[K[Live Temps] H1: %.2f°C | H2: %.2f°C | Mon: %.2f°C", global_temp1, global_temp2, global_temp3);
                    fflush(stdout);
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
void* endstop_poll_thread(void* arg) {
    while (program_running) {
        write(serial_fd_global, "?\n", 2);
        usleep(5000); // 5ms poll
    }
    return NULL;
}

// --- Identical Homing Logic for Motor 1 & Motor 2 ---
void home_motor1() {
    lgGpioWrite(motor1.gpio_handle, motor1.dir_pin, 1); // Dir 1 for Motor 1 homing
    while (!endstop1_hit_flag && program_running) {
        lgGpioWrite(motor1.gpio_handle, motor1.step_pin, 1);
        usleep(600);
        lgGpioWrite(motor1.gpio_handle, motor1.step_pin, 0);
        usleep(600);
    }
    usleep(50000); 
}

void home_motor2() {
    lgGpioWrite(motor2.gpio_handle, motor2.dir_pin, 0); // Dir 0 for Tray IN limit switch
    while (!endstop2_hit_flag && program_running) {
        lgGpioWrite(motor2.gpio_handle, motor2.step_pin, 1);
        usleep(600);
        lgGpioWrite(motor2.gpio_handle, motor2.step_pin, 0);
        usleep(600);
    }
    usleep(50000); 
}

// --- Detached Thread Functions for Tray Commands ---
// We wrap these in threads so the terminal doesn't freeze while the tray moves
void* tray_in_task(void* arg) {
    printf("\n>>> Moving Tray IN to limit switch...\n");
    home_motor2(); // Uses exactly the same logic as Motor 1 homing
    printf("\n>>> Tray IN Complete.\n");
    return NULL;
}

void* tray_out_task(void* arg) {
    printf("\n>>> Moving Tray OUT (1000 steps)...\n");
    DRV8825_Turn(&motor2, 3000, 1, 600); // Uses exactly the same library call as Motor 1
    printf("\n>>> Tray OUTi Complete.\n");
    return NULL;
}

// --- THREAD 3: Terminal Listener ---
void* terminal_thread(void* arg) {
    char input[256];
    while (program_running) {
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strcspn(input, "\n")] = 0; 
            
            if (strcmp(input, "start heaters") == 0) {
                printf("\n>>> Command accepted: STARTING HEATERS\n");
                write(serial_fd_global, "START\n", 6);
            } 
            else if (strcmp(input, "stop heater") == 0) {
                printf("\n>>> Command accepted: STOPPING HEATERS\n");
                write(serial_fd_global, "STOP\n", 5);
            }
            else if (strcmp(input, "tray in") == 0) {
                pthread_t t;
                pthread_create(&t, NULL, tray_in_task, NULL);
                pthread_detach(t);
            }
            else if (strcmp(input, "tray out") == 0) {
                pthread_t t;
                pthread_create(&t, NULL, tray_out_task, NULL);
                pthread_detach(t);
            }
            else if (strcmp(input, "start optical") == 0) {
                printf("\n>>> Command accepted: STARTING OPTICAL SEQUENCE\n");
                start_optical_flag = 1;
            }
        }
    }
    return NULL;
}

// --- Helper: Optical Sensor Averaging ---
void take_optical_reading(int cycle, int spot, int led_state, int i2c_handle, FILE* csv) {
    lgGpioWrite(gpio_handle_global, PIN_12V_EN, 1);
    lgGpioWrite(gpio_handle_global, PIN_LED_EN, led_state);
    
    float total_voltage = 0.0;
    long total_adc = 0;

    for(int i = 0; i < 10; i++) {
        uint8_t config[3];
        config[0] = REG_CONFIG;
        config[1] = 0xC3; 
        config[2] = 0x83; 
        lgI2cWriteDevice(i2c_handle, (char*)config, 3);
        
        usleep(25000); // 25ms conversion wait

        lgI2cWriteByte(i2c_handle, REG_CONVERSION);
        int raw_data = lgI2cReadWordData(i2c_handle, REG_CONVERSION);
        
        if (raw_data >= 0) {
            int16_t raw_adc = (raw_data >> 8) | ((raw_data & 0xFF) << 8);
            float voltage = (raw_adc * 4.096) / 32768.0;
            total_adc += raw_adc;
            total_voltage += voltage;
        }
        
        usleep(475000); // 475ms wait for 500ms total loop
    }

    int16_t avg_adc = total_adc / 10;
    float avg_voltage = total_voltage / 10.0;

    printf("\n    -> [Spot %d Avg Data] ADC: %d | Volts: %.4f V | LED: %s\n", 
            spot, avg_adc, avg_voltage, led_state ? "ON" : "OFF");
    
    fprintf(csv, "%d,%d,%.2f,%.2f,%.2f,%d,%.4f\n", 
            cycle, spot, global_temp1, global_temp2, global_temp3, avg_adc, avg_voltage);
    fflush(csv); 

    lgGpioWrite(gpio_handle_global, PIN_LED_EN, 0);
    lgGpioWrite(gpio_handle_global, PIN_12V_EN, 0);
}

// --- Main Program ---
int main() {
    serial_fd_global = setup_serial(SERIAL_PORT);
    if (serial_fd_global < 0) {
        printf("Failed to open Serial Port.\n");
        return -1;
    }
    tcflush(serial_fd_global, TCIOFLUSH);
    sleep(2); 

    gpio_handle_global = lgGpiochipOpen(0);
    if (gpio_handle_global < 0) {
        printf("Failed to open GPIO. Run with sudo.\n");
        return -1;
    }
    
    lgGpioClaimOutput(gpio_handle_global, 0, PIN_12V_EN, 0);
    lgGpioClaimOutput(gpio_handle_global, 0, PIN_LED_EN, 0);

    // Explicitly Initialize Both Steppers 
    DRV8825_Init(&motor1, gpio_handle_global, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    DRV8825_Init(&motor2, gpio_handle_global, MOTOR2_DIR_PIN, MOTOR2_STEP_PIN, MOTOR2_ENABLE_PIN);
    
    // Enable Both Steppers
    DRV8825_Enable(&motor1, 1);
    DRV8825_Enable(&motor2, 1);
    usleep(50000);
    
    int i2c_handle = lgI2cOpen(1, ADS1115_ADDR, 0);
    if (i2c_handle < 0) {
        printf("Failed to open I2C bus.\n");
        return -1;
    }

    FILE *csv_file = fopen("experiment_log.csv", "w");
    if (!csv_file) return -1;
    fprintf(csv_file, "Cycle,Spot,Temp_H1(C),Temp_H2(C),Temp_Mon(C),Opt_ADC_Avg,Opt_Volts_Avg\n");

    pthread_t read_th, poll_th, term_th;
    pthread_create(&read_th, NULL, serial_read_thread, NULL);
    pthread_create(&poll_th, NULL, endstop_poll_thread, NULL);
    pthread_create(&term_th, NULL, terminal_thread, NULL);

    printf("\nSystem Ready. Awaiting commands ('start heaters', 'tray in', 'tray out', 'start optical').\n");

    // Idle loop waiting for the user to type 'start optical'
    while (!start_optical_flag && program_running) {
        usleep(100000); 
    }

    if (program_running) {
        struct timespec target_time;
        clock_gettime(CLOCK_MONOTONIC, &target_time);

        // --- The 60-Minute Master Loop ---
        for (int cycle = 1; cycle <= 60; cycle++) {
            printf("\n======================================================\n");
            printf("=== CYCLE %d === (Synchronized to 60.0s interval)\n", cycle);


            
            // 1. Dark Current
            home_motor1();
            printf("    -> Moving 1000 steps Dir 0 to Spot 0 (Dark Current)...\n");
            DRV8825_Turn(&motor1, 1310, 0, 600);
            take_optical_reading(cycle, 0, 0, i2c_handle, csv_file); // LED OFF (0)

            // 2. Spot 1
            home_motor1();
            printf("    -> Moving 200 steps Dir 0 to Spot 1...\n");
            DRV8825_Turn(&motor1, 75, 0, 600);
            take_optical_reading(cycle, 1, 1, i2c_handle, csv_file); // LED ON (1)

            // 3. Spots 2 through 6
            for (int spot = 2; spot <= 6; spot++) {
                printf("    -> Moving 50 steps Dir 0 to Spot %d...\n", spot);
                DRV8825_Turn(&motor1, 167, 0, 600);
                take_optical_reading(cycle, spot, 1, i2c_handle, csv_file); // LED ON (1)
            }

            printf("    -> All spots complete. Homing Motor 1 to conclude sequence...\n");
            home_motor1();

            // Time Synchronization for the NEXT cycle (Exactly 60s from the start of this cycle)
            target_time.tv_sec += 60;
            
            if (cycle < 60) {
                printf("    -> Idling. Waiting for exact 60s boundary to start next cycle...\n");
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target_time, NULL);
            }
        }
    }

    printf("\nExperiment Complete. Shutting down hardware...\n");
    write(serial_fd_global, "STOP\n", 5); // Stop heaters
    program_running = 0; 
    
    pthread_cancel(term_th); 
    pthread_join(read_th, NULL);
    pthread_join(poll_th, NULL);

    DRV8825_Stop(&motor1);
    DRV8825_Stop(&motor2);
    lgI2cClose(i2c_handle);
    lgGpiochipClose(gpio_handle_global);
    close(serial_fd_global);
    fclose(csv_file);

    printf("Sending Data Log via Email...\n");
    system("echo 'Experiment has concluded successfully.' | mutt -s 'Arduino/Pi Experiment Data' -a experiment_log.csv -- rajatsrivastavaaxon@gmail.com shahinitin017@gmail.com onlyunacademy@gmail.com");

    return 0;
}
