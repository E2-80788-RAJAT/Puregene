#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <pigpio.h>

// Include Waveshare DRV8825 Library Header
#include "drv8825.h"

// --- GPIO PIN DEFINITIONS (BCM Numbering for non-stepper peripherals) ---
#define PIN_LIMIT_SWITCH   22
#define PIN_12V_SUPPLY     17
#define PIN_LED            13

// --- ADS1115 I2C CONFIGURATION ---
#define I2C_BUS            "/dev/i2c-1"
#define ADS1115_ADDR       0x48
#define ADS1115_REG_CONV   0x00
#define ADS1115_REG_CONFIG 0x01

// --- STEPPER MOTOR CONSTANTS (using DRV8825 library definitions) ---
#define MOTOR_ID           MOTOR1
#define STEP_DELAY_MS      2     // Delay between steps in ms
#define HOMING_OFFSET_STEPS 500
#define SPOT_INTERVAL_STEPS 100

// --- EXPERIMENT PARAMETERS ---
#define TOTAL_CYCLES       20
#define SPOTS_PER_CYCLE    8
#define READINGS_PER_SPOT  5
#define READING_DELAY_S    1

// --- FUNCTION PROTOTYPES ---
int ads1115_init(const char *bus, uint8_t addr);
int ads1115_read_a0(int fd, int16_t *raw_adc, double *voltage);
void home_stepper(void);

// ============================================================================
// STEPPER HOMING FUNCTION (USING DRV8825 LIBRARY)
// ============================================================================

void home_stepper(void) {
    printf("[HOME] Moving stepper motor toward limit switch (GPIO %d)...\n", PIN_LIMIT_SWITCH);

    // Step 1 step at a time toward the limit switch until triggered
    while (gpioRead(PIN_LIMIT_SWITCH) == 1) {
        DRV8825_TurnStep(MOTOR_ID, FORWARD, 1, STEP_DELAY_MS);
    }

    printf("[HOME] Limit switch triggered!\n");
    time_sleep(0.5);

    printf("[HOME] Moving %d steps in opposite direction to reach Spot 1...\n", HOMING_OFFSET_STEPS);
    DRV8825_TurnStep(MOTOR_ID, BACKWARD, HOMING_OFFSET_STEPS, STEP_DELAY_MS);
    printf("[HOME] Reached Spot 1.\n");
}

// ============================================================================
// ADS1115 I2C FUNCTIONS
// ============================================================================

int ads1115_init(const char *bus, uint8_t addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("[ERROR] Failed to open I2C bus");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("[ERROR] Failed to connect to ADS1115 sensor");
        close(fd);
        return -1;
    }
    return fd;
}

int ads1115_read_a0(int fd, int16_t *raw_adc, double *voltage) {
    // Config Register:
    // Single-shot, AIN0 single-ended, +/- 4.096V range (LSB = 0.125 mV), 128 SPS
    uint8_t config_cmd[3] = {ADS1115_REG_CONFIG, 0xC3, 0x83};
    if (write(fd, config_cmd, 3) != 3) {
        perror("[ERROR] ADS1115 config write failed");
        return -1;
    }

    // Allow conversion time (15 ms)
    time_sleep(0.015);

    uint8_t reg_ptr = ADS1115_REG_CONV;
    if (write(fd, &reg_ptr, 1) != 1) {
        perror("[ERROR] ADS1115 register pointer write failed");
        return -1;
    }

    uint8_t data[2];
    if (read(fd, data, 2) != 2) {
        perror("[ERROR] ADS1115 data read failed");
        return -1;
    }

    *raw_adc = (int16_t)((data[0] << 8) | data[1]);
    *voltage = (*raw_adc) * 0.000125; // Voltage conversion: +/- 4.096V -> 0.125mV/bit
    return 0;
}

// ============================================================================
// MAIN APPLICATION
// ============================================================================

int main(void) {
    // 1. Initialize pigpio for peripheral control (Limit Switch, LED, 12V Supply)
    if (gpioInitialise() < 0) {
        fprintf(stderr, "[ERROR] pigpio initialization failed. Did you run with sudo?\n");
        return EXIT_FAILURE;
    }

    gpioSetMode(PIN_LIMIT_SWITCH, PI_INPUT);
    gpioSetPullUpDown(PIN_LIMIT_SWITCH, PI_PUD_UP); // Internal pull-up for active-low switch

    gpioSetMode(PIN_12V_SUPPLY, PI_OUTPUT);
    gpioSetMode(PIN_LED, PI_OUTPUT);

    // Initial peripheral state: OFF
    gpioWrite(PIN_12V_SUPPLY, 0);
    gpioWrite(PIN_LED, 0);

    // 2. Initialize Waveshare DRV8825 Motor Driver
    if (DRV8825_Init() != 0) {
        fprintf(stderr, "[ERROR] DRV8825 initialization failed.\n");
        gpioTerminate();
        return EXIT_FAILURE;
    }
    DRV8825_SelectMotor(MOTOR_ID);

    // 3. Open ADS1115 I2C Connection
    int i2c_fd = ads1115_init(I2C_BUS, ADS1115_ADDR);
    if (i2c_fd < 0) {
        DRV8825_Stop(MOTOR_ID);
        gpioTerminate();
        return EXIT_FAILURE;
    }

    // 4. Create Excel-compatible CSV Log File
    FILE *log_file = fopen("optical_readings_log.csv", "w");
    if (!log_file) {
        perror("[ERROR] Failed to create Excel log file");
        close(i2c_fd);
        DRV8825_Stop(MOTOR_ID);
        gpioTerminate();
        return EXIT_FAILURE;
    }

    // Write Header Row for Excel
    fprintf(log_file, "Cycle,Spot,Average_Raw_ADC,Average_Voltage_V\n");
    fflush(log_file);

    printf("==================================================\n");
    printf("   OPTICAL READER (DRV8825 LIB | 20 CYCLES)\n");
    printf("==================================================\n");

    // 5. Home the Stepper Motor
    home_stepper();

    // 6. Execute 20 Measurement Cycles
    for (int cycle = 1; cycle <= TOTAL_CYCLES; cycle++) {
        printf("\n---> STARTING CYCLE %d of %d <---\n", cycle, TOTAL_CYCLES);

        for (int spot = 1; spot <= SPOTS_PER_CYCLE; spot++) {
            printf("\n--- Cycle %d | Spot %d ---\n", cycle, spot);

            // Turn ON 12V Supply and LED
            gpioWrite(PIN_12V_SUPPLY, 1);
            gpioWrite(PIN_LED, 1);
            time_sleep(0.1); // Allow photodiode and lighting to stabilize

            long sum_adc = 0;
            double sum_voltage = 0.0;

            // Take 5 readings at 1-second intervals
            for (int r = 1; r <= READINGS_PER_SPOT; r++) {
                int16_t raw_adc = 0;
                double voltage = 0.0;

                if (ads1115_read_a0(i2c_fd, &raw_adc, &voltage) == 0) {
                    sum_adc += raw_adc;
                    sum_voltage += voltage;
                    printf("  Reading %d/%d: Raw ADC = %5d | Voltage = %.4f V\n", r, READINGS_PER_SPOT, raw_adc, voltage);
                } else {
                    printf("  Reading %d/%d: [READ ERROR]\n", r, READINGS_PER_SPOT);
                }

                if (r < READINGS_PER_SPOT) {
                    time_sleep(READING_DELAY_S);
                }
            }

            // Calculate 5-second Average
            double avg_adc = (double)sum_adc / READINGS_PER_SPOT;
            double avg_voltage = sum_voltage / READINGS_PER_SPOT;

            printf("  >> SPOT %d AVERAGE -> Raw ADC: %.1f | Voltage: %.4f V\n", spot, avg_adc, avg_voltage);

            // Log Data to Excel Sheet (CSV)
            fprintf(log_file, "%d,%d,%.2f,%.6f\n", cycle, spot, avg_adc, avg_voltage);
            fflush(log_file);

            // Turn OFF LED and 12V Supply
            gpioWrite(PIN_LED, 0);
            gpioWrite(PIN_12V_SUPPLY, 0);

            // Advance 100 steps to the next spot
            if (spot < SPOTS_PER_CYCLE) {
                printf("  Moving 100 steps to next spot...\n");
                DRV8825_TurnStep(MOTOR_ID, BACKWARD, SPOT_INTERVAL_STEPS, STEP_DELAY_MS);
            }
        }

        // Return to Spot 1 at the end of each cycle
        if (cycle < TOTAL_CYCLES) {
            printf("\n[CYCLE COMPLETE] Returning to Spot 1 for next cycle...\n");
            DRV8825_TurnStep(MOTOR_ID, FORWARD, SPOT_INTERVAL_STEPS * (SPOTS_PER_CYCLE - 1), STEP_DELAY_MS);
            time_sleep(1.0);
        }
    }

    printf("\n==================================================\n");
    printf("   ALL 20 CYCLES COMPLETED SUCCESSFULLY!\n");
    printf("   Data logged in: optical_readings_log.csv\n");
    printf("==================================================\n");

    // 7. Cleanup & Shutdown
    DRV8825_Stop(MOTOR_ID);
    fclose(log_file);
    close(i2c_fd);
    gpioTerminate();

    return EXIT_SUCCESS;
}
