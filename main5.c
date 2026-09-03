
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
#define PIN_12V_EN 17 // GPIO pin for 12V Enable
#define PIN_LED_EN 27 // GPIO pin for LED PWM/CTRL

#define ADS1115_ADDR 0x48
#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

// --- Global Flags & Data ---
volatile int endstop_hit_flag = 0;
volatile int program_running = 1;
int serial_fd_global = -1;

// Thread-safe storage for the latest temperature readings
volatile float global_temp1 = 0.0;
volatile float global_temp2 = 0.0;

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
                    endstop_hit_flag = (buf[1] == '1') ? 1 : 0;
                } else if (buf[0] == 'T') {
                    // Parse live temperatures into global variables
                    sscanf(&buf[1], "%f,%f", (float*)&global_temp1, (float*)&global_temp2);
                    printf("\r\033[K[Live] H1: %.2f°C | H2: %.2f°C", global_temp1, global_temp2);
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
        usleep(5000); 
    }
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
            } else if (strcmp(input, "stop heater") == 0) {
                printf("\n>>> Command accepted: STOPPING HEATERS\n");
                write(serial_fd_global, "STOP\n", 5);
            }
        }
    }
    return NULL;
}

// --- Helper: Homing ---
void home_motor(StepperMotor* motor) {
    lgGpioWrite(motor->gpio_handle, motor->dir_pin, 1);
    while (!endstop_hit_flag) {
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 1);
        usleep(600);
        lgGpioWrite(motor->gpio_handle, motor->step_pin, 0);
        usleep(600);
    }
    usleep(50000); 
}

// --- Helper: Optical Sensor Reading ---
void take_optical_reading(int cycle, int spot, int gpio_handle, int i2c_handle, FILE* csv) {
    printf("\n    -> [Spot %d] Powering up Optical Sensor for 5 seconds...\n", spot);
    
    // 1. Power ON
    lgGpioWrite(gpio_handle, PIN_12V_EN, 1);
    lgGpioWrite(gpio_handle, PIN_LED_EN, 1);
    
    // 2. Wait exactly 5 seconds as requested
    sleep(5); 

    // 3. Trigger ADS1115 Conversion
    uint8_t config[3];
    config[0] = REG_CONFIG;
    config[1] = 0xC3; // MSB
    config[2] = 0x83; // LSB
    lgI2cWriteDevice(i2c_handle, (char*)config, 3);
    
    usleep(25000); // Wait 25ms for conversion

    // 4. Read Data
    lgI2cWriteByte(i2c_handle, REG_CONVERSION);
    int raw_data = lgI2cReadWordData(i2c_handle, REG_CONVERSION);
    
    int16_t raw_adc = 0;
    float voltage = 0.0;

    if (raw_data >= 0) {
        // Swap bytes for Endianness
        raw_adc = (raw_data >> 8) | ((raw_data & 0xFF) << 8);
        voltage = (raw_adc * 4.096) / 32768.0;
        
        printf("    -> [Spot %d Data] ADC: %d | Voltage: %.4f V | Temps: %.2f°C, %.2f°C\n", 
                spot, raw_adc, voltage, global_temp1, global_temp2);
        
        // Log to CSV
        fprintf(csv, "%d,%d,%.2f,%.2f,%d,%.4f\n", 
                cycle, spot, global_temp1, global_temp2, raw_adc, voltage);
        fflush(csv); // Save to disk immediately
    } else {
        printf("    -> [Spot %d] Error reading I2C!\n", spot);
    }

    // 5. Power OFF
    lgGpioWrite(gpio_handle, PIN_LED_EN, 0);
    lgGpioWrite(gpio_handle, PIN_12V_EN, 0);
}

// --- Main Program ---
int main() {
    // 1. Setup Serial
    serial_fd_global = setup_serial(SERIAL_PORT);
    if (serial_fd_global < 0) {
        printf("Failed to open Serial Port.\n");
        return -1;
    }
    tcflush(serial_fd_global, TCIOFLUSH);
    sleep(2); 

    // 2. Setup GPIO
    int handle = lgGpiochipOpen(0);
    if (handle < 0) {
        printf("Failed to open GPIO. Run with sudo.\n");
        return -1;
    }
    
    // Claim optical sensor pins
    lgGpioClaimOutput(handle, 0, PIN_12V_EN, 0);
    lgGpioClaimOutput(handle, 0, PIN_LED_EN, 0);

    // Setup Stepper
    StepperMotor motor1;
    DRV8825_Init(&motor1, handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    DRV8825_Enable(&motor1, 1);
    
    // 3. Setup I2C
    int i2c_handle = lgI2cOpen(1, ADS1115_ADDR, 0);
    if (i2c_handle < 0) {
        printf("Failed to open I2C bus.\n");
        return -1;
    }

    // 4. Initialize CSV Data Log
    FILE *csv_file = fopen("experiment_log.csv", "w");
    if (!csv_file) {
        printf("Failed to create CSV file.\n");
        return -1;
    }
    fprintf(csv_file, "Cycle,Spot,Temp_H1(C),Temp_H2(C),Optical_ADC,Optical_Volts\n");

    // 5. Start Threads
    pthread_t read_th, poll_th, term_th;
    pthread_create(&read_th, NULL, serial_read_thread, NULL);
    pthread_create(&poll_th, NULL, endstop_poll_thread, NULL);
    pthread_create(&term_th, NULL, terminal_thread, NULL);

    printf("\nSystem Ready. Data will log to 'experiment_log.csv'.\n");
    printf("Type 'start heaters' or 'stop heater' anytime.\n");

    struct timespec target_time;
    clock_gettime(CLOCK_MONOTONIC, &target_time);

    // --- The 60-Minute Master Loop ---
    for (int cycle = 1; cycle <= 6; cycle++) {
        printf("\n======================================================\n");
        printf("=== CYCLE %d === (Synchronized to 60.0s interval)\n", cycle);
        
        home_motor(&motor1);

        // Move to Dark Current Spot and Read (Takes ~5 sec)
        printf("    -> Moving to Spot 0 (Dark Current)...\n");
        DRV8825_Turn(&motor1, 1000, 0, 600);
        take_optical_reading(cycle, 0, handle, i2c_handle, csv_file);

        // Move to the 6 scanning spots
        for (int spot = 1; spot <= 6; spot++) {
            printf("    -> Moving to Spot %d...\n", spot);
            DRV8825_Turn(&motor1, 100, 1, 600);
            take_optical_reading(cycle, spot, handle, i2c_handle, csv_file);
        }

        printf("    -> All spots complete. Homing...\n");
        home_motor(&motor1);

        target_time.tv_sec += 60;
        if (cycle < 60) {
            printf("    -> Idling at home. Waiting for exact 60s boundary...\n");
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target_time, NULL);
        }
    }

    printf("\nExperiment Complete. Shutting down hardware...\n");
    
    // Safety shutdowns
    write(serial_fd_global, "STOP\n", 5);
    program_running = 0; 
    
    pthread_cancel(term_th); 
    pthread_join(read_th, NULL);
    pthread_join(poll_th, NULL);

    DRV8825_Stop(&motor1);
    lgI2cClose(i2c_handle);
    lgGpiochipClose(handle);
    close(serial_fd_global);
    fclose(csv_file);

    // 6. Send the Email via Terminal System Command
    printf("Sending Data Log via Email...\n");
    int mail_status = system("echo 'Experiment has concluded successfully. Please find the attached data log.' | mutt -s 'Arduino/Pi Experiment Data' -a experiment_log.csv -- rajatsrivastavaaxon@gmail.com shahinitin017@gmail.com onlyunacademy@gmail.com");
    
    if (mail_status == 0) {
        printf("Email sent successfully!\n");
    } else {
        printf("Failed to send email. Ensure 'mutt' and 'msmtp' are configured on the Pi.\n");
    }

    return 0;
}
