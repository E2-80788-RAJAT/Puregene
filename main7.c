#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <lgpio.h>

// Include your custom DRV8825 header
#include "drv8825.h"

// --- GPIO PIN DEFINITIONS (BCM Numbering) ---
#define PIN_LIMIT_SWITCH    22
#define PIN_12V_SUPPLY      17
#define PIN_LED             27  // Note: Set to 27 to avoid conflict with MOTOR1_DIR_PIN (13) in drv8825.h

// --- ADS1115 I2C CONFIGURATION ---
#define I2C_BUS             "/dev/i2c-1"
#define ADS1115_ADDR        0x48
#define ADS1115_REG_CONV    0x00
#define ADS1115_REG_CONFIG  0x01

// --- STEPPER MOTOR SETTINGS ---
#define DIR_TOWARD_SWITCH   0   // Backward toward switch
#define DIR_AWAY_SWITCH     1   // Forward away from switch
#define STEP_DELAY_US       1000 // 1 ms delay between step pulses
#define HOMING_OFFSET_STEPS 600
#define SPOT_INTERVAL_STEPS 100

// --- EXPERIMENT PARAMETERS ---
#define TOTAL_CYCLES        20
#define SPOTS_PER_CYCLE     8
#define READINGS_PER_SPOT   5
#define READING_DELAY_US    1000000 // 1 second in microseconds

// --- FUNCTION PROTOTYPES ---
int ads1115_init(const char *bus, uint8_t addr);
int ads1115_read_a0(int fd, int16_t *raw_adc, double *voltage);
void home_stepper(StepperMotor *motor, int gpio_handle);

// ============================================================================
// HOMING ROUTINE USING YOUR DRV8825 FUNCTIONS
// ============================================================================

void home_stepper(StepperMotor *motor, int gpio_handle) {
    printf("[HOME] Moving stepper toward limit switch (GPIO %d)...\n", PIN_LIMIT_SWITCH);

    // Turn 1 step at a time until the active-low limit switch triggers (reads 0)
    while (lgGpioRead(gpio_handle, PIN_LIMIT_SWITCH) == 1) {
        DRV8825_Turn(motor, 1, DIR_TOWARD_SWITCH, STEP_DELAY_US);
    }

    printf("[HOME] Limit switch triggered!\n");
    usleep(500000); // 0.5s pause

    printf("[HOME] Moving %d steps in opposite direction to reach Spot 1...\n", HOMING_OFFSET_STEPS);
    DRV8825_Turn(motor, HOMING_OFFSET_STEPS, DIR_AWAY_SWITCH, STEP_DELAY_US);
    printf("[HOME] Reached Spot 1.\n");
}

// ============================================================================
// ADS1115 I2C DRIVER
// ============================================================================

int ads1115_init(const char *bus, uint8_t addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("[ERROR] Failed to open I2C bus /dev/i2c-1");
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
    // Configure ADS1115: Single-shot, AIN0 single-ended, +/- 4.096V range, 128 SPS
    // MSB = 0xC3, LSB = 0x83
    uint8_t config_cmd[3] = {ADS1115_REG_CONFIG, 0xC3, 0x83};
    if (write(fd, config_cmd, 3) != 3) {
        perror("[ERROR] ADS1115 config write failed");
        return -1;
    }

    // Wait 15 ms for conversion to finish
    usleep(15000);

    uint8_t reg_ptr = ADS1115_REG_CONV;
    if (write(fd, &reg_ptr, 1) != 1) {
        perror("[ERROR] ADS1115 register pointer write failed");
        return -1;
    }

    uint8_t data[2];
    if (read(fd, data, 2) != 2) {
	perror("sd	    115 data read failed");
        return -1;
    }

    *raw_adc = (int16_t)((data[0] << 8) | data[1]);
    *voltage = (*raw_adc) * 0.000125; // +/- 4.096V range -> 0.125mV/LSB
    return 0;
}

// ============================================================================
// MAIN AUTOMATION ROUTINE
// ============================================================================

int main(void) {
    // 1. Open lgpio chip handle (gpiochip0 is standard on Raspberry Pi 4B)
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        fprintf(stderr, "[ERROR] Could not open GPIO chip. Run with sudo or check permissions.\n");
        return EXIT_FAILURE;
    }

    // 2. Claim peripheral GPIO pins
    // Limit switch as input with internal pull-up resistor enabled
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, PIN_LIMIT_SWITCH);
    lgGpioClaimOutput(gpio_handle, 0, PIN_12V_SUPPLY, 0);
    lgGpioClaimOutput(gpio_handle, 0, PIN_LED, 0);

    // 3. Initialize Waveshare DRV8825 Motor using your library function
    StepperMotor motor;
    if (DRV8825_Init(&motor, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN) != 0) {
        fprintf(stderr, "[ERROR] DRV8825_Init failed.\n");
        lgGpiochipClose(gpio_handle);
        return EXIT_FAILURE;
    }

    // Enable motor (Rev 2.1+ boards use 1 to enable per your comments)
    DRV8825_Enable(&motor, 1);

    // 4. Initialize ADS1115 over I2C
    int i2c_fd = ads1115_init(I2C_BUS, ADS1115_ADDR);
    if (i2c_fd < 0) {
        DRV8825_Stop(&motor);
        lgGpiochipClose(gpio_handle);
        return EXIT_FAILURE;
    }

    // 5. Create Excel-compatible CSV Log File
    FILE *log_file = fopen("optical_readings_log.csv", "w");
    if (!log_file) {
        perror("[ERROR] Could not open log file");
        close(i2c_fd);
        DRV8825_Stop(&motor);
        lgGpiochipClose(gpio_handle);
        return EXIT_FAILURE;
    }

    // Write CSV Header
    fprintf(log_file, "Cycle,Spot,Average_Raw_ADC,Average_Voltage_V\n");
    fflush(log_file);

    printf("==================================================\n");
    printf("   OPTICAL READER (20 CYCLES / 8 SPOTS)\n");
    printf("==================================================\n");

    // 6. Perform homing to reach Spot 1
    home_stepper(&motor, gpio_handle);

    // 7. Execute 20 Measurement Cycles
    for (int cycle = 1; cycle <= TOTAL_CYCLES; cycle++) {
        printf("\n---> STARTING CYCLE %d of %d <---\n", cycle, TOTAL_CYCLES);

        for (int spot = 1; spot <= SPOTS_PER_CYCLE; spot++) {
            printf("\n--- Cycle %d | Spot %d ---\n", cycle, spot);

            // Turn ON 12V Supply and LED
            lgGpioWrite(gpio_handle, PIN_12V_SUPPLY, 1);
            lgGpioWrite(gpio_handle, PIN_LED, 1);
            usleep(100000); // 100 ms stabilization delay

            long sum_adc = 0;
            double sum_voltage = 0.0;

            // Take 5 readings at 1-second intervals
            for (int r = 1; r <= READINGS_PER_SPOT; r++) {
                int16_t raw_adc = 0;
                double voltage = 0.0;

                if (ads1115_read_a0(i2c_fd, &raw_adc, &voltage) == 0) {
                    sum_adc += raw_adc;
                    sum_voltage += voltage;
                    printf("  Reading %d/%d: Raw ADC = %5d | Voltage = %.4f V\n", 
                           r, READINGS_PER_SPOT, raw_adc, voltage);
                } else {
                    printf("  Reading %d/%d: [READ ERROR]\n", r, READINGS_PER_SPOT);
                }

                if (r < READINGS_PER_SPOT) {
                    usleep(READING_DELAY_US);
                }
            }

            // Calculate 5-second Average
            double avg_adc = (double)sum_adc / READINGS_PER_SPOT;
            double avg_voltage = sum_voltage / READINGS_PER_SPOT;

            printf("  >> SPOT %d AVERAGE -> Raw ADC: %.1f | Voltage: %.4f V\n", 
                   spot, avg_adc, avg_voltage);

            // Log data to Excel CSV file
            fprintf(log_file, "%d,%d,%.2f,%.6f\n", cycle, spot, avg_adc, avg_voltage);
            fflush(log_file);

            // Turn OFF LED and 12V Supply
            lgGpioWrite(gpio_handle, PIN_LED, 0);
            lgGpioWrite(gpio_handle, PIN_12V_SUPPLY, 0);

            // Move 100 steps forward to reach the next spot
            if (spot < SPOTS_PER_CYCLE) {
                printf("  Moving 100 steps to next spot...\n");
                DRV8825_Turn(&motor, SPOT_INTERVAL_STEPS, DIR_AWAY_SWITCH, STEP_DELAY_US);
            }
        }

        // Return to Spot 1 at the end of each cycle
        if (cycle < TOTAL_CYCLES) {
            printf("\n[CYCLE COMPLETE] Returning to Spot 1 for next cycle...\n");
            int return_steps = SPOT_INTERVAL_STEPS * (SPOTS_PER_CYCLE - 1);
            DRV8825_Turn(&motor, return_steps, DIR_TOWARD_SWITCH, STEP_DELAY_US);
            usleep(1000000); // 1-second rest before next cycle
        }
    }

    printf("\n==================================================\n");
    printf("   ALL 20 CYCLES COMPLETED SUCCESSFULLY!\n");
    printf("   Data logged in: optical_readings_log.csv\n");
    printf("==================================================\n");

    // 8. Safe Shutdown & Cleanup
    DRV8825_Stop(&motor);
    fclose(log_file);
    close(i2c_fd);
    lgGpiochipClose(gpio_handle);

    return EXIT_SUCCESS;
}
