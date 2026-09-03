#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include <stdint.h>
#include "drv8825.h"

// --- Pin Definitions ---
#define LIMIT_SWITCH_PIN 24
#define POWER_12V_PIN    17
#define LED_PIN          13 

// --- I2C / ADS1115 Definitions ---
#define I2C_BUS          1
#define ADS1115_ADDR     0x48
#define REG_CONVERSION   0x00
#define REG_CONFIG       0x01

// --- Function to read the ADS1115 with Retry Logic ---
int read_ads1115(int i2c_handle) {
    uint8_t config[3];
    config[0] = REG_CONFIG;
    config[1] = 0xC3;
    config[2] = 0x83;
    
    int w_err = -1;
    
    // Retry mechanism: Try up to 3 times if the device NACKs
    for(int attempt = 0; attempt < 3; attempt++) {
        w_err = lgI2cWriteDevice(i2c_handle, (char*)config, 3);
        if (w_err >= 0) break; // Success
        usleep(50000); // Wait 50ms before retrying
    }
    
    if (w_err < 0) {
        printf("     [I2C ERROR] Failed to write config (NACK). Code: %d\n", w_err);
        return 0; // Fallback
    }
    
    // Wait for the conversion to complete
    usleep(25000); 

    // Point to Conversion Register and read word data
    lgI2cWriteByte(i2c_handle, REG_CONVERSION);
    int raw_data = lgI2cReadWordData(i2c_handle, REG_CONVERSION);
    
    if (raw_data >= 0) {
        // Swap bytes (ADS1115 sends MSB first, lgpio reads LSB first)
        int16_t raw_adc = (raw_data >> 8) | ((raw_data & 0xFF) << 8);
        return raw_adc;
    } else {
        printf("     [I2C ERROR] Failed to read data. Code: %d\n", raw_data);
        return 0; // Fallback
    }
}

int main() {
    // 1. Open GPIO and I2C chips
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        printf("Error: Failed to open gpiochip. Make sure to run with sudo.\n");
        return -1;
    }

    int i2c_handle = lgI2cOpen(I2C_BUS, ADS1115_ADDR, 0);
    if (i2c_handle < 0) {
        printf("Error: Failed to open I2C bus for ADS1115.\n");
        lgGpiochipClose(gpio_handle);
        return -1;
    }

    // 2. Initialize Hardware
    StepperMotor motor1;
    DRV8825_Init(&motor1, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
    
    // Setup auxiliary pins
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN);
    lgGpioClaimOutput(gpio_handle, 0, POWER_12V_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, LED_PIN, 0);

    // 3. Prepare CSV Log File
    FILE *log_file = fopen("optical_data_log.csv", "w");
    if (log_file == NULL) {
        printf("Error: Cannot create CSV file.\n");
        return -1;
    }
    // Write CSV Headers (Logging only average values)
    fprintf(log_file, "Cycle,Spot,Dark_Current_Raw,Avg_ADC_Raw,Avg_Voltage_V\n");
    fflush(log_file);

    DRV8825_Enable(&motor1, 1); 
    usleep(500000); 

    // 4. Main Routine: 20 Cycles
    for (int cycle = 1; cycle <= 20; cycle++) {
        printf("\n--- Starting Cycle %d of 20 ---\n", cycle);

        // A. Homing Sequence
        printf("Homing motor...\n");
        lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 0);
        while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1) {
            lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1);
            usleep(600); 
            lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0);
            usleep(600); 
        }
        sleep(1); // Pause after hitting limit switch

        // B. Calculate Dark Current for this cycle 
        // 12V ON to power the module, LED OFF
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
        lgGpioWrite(gpio_handle, LED_PIN, 0);
        
        // Increased delay to 300ms to allow the ADS1115/optical sensor to fully boot up
        usleep(300000); 
        
        int dark_current_raw = read_ads1115(i2c_handle);
        printf("Cycle %d Dark Current baseline: %d\n", cycle, dark_current_raw);
        
        // Turn 12V OFF before moving
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 0); 

        // C. Move to First Spot (500 steps in dir 1)
        printf("Moving 500 steps to Spot 1...\n");
        DRV8825_Turn(&motor1, 500, 1, 600);
        sleep(1);

        // D. Iterate through 8 Spots
        for (int spot = 1; spot <= 8; spot++) {
            printf("  -> Taking readings at Spot %d...\n", spot);

            // Turn ON 12V and LED
            lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
            lgGpioWrite(gpio_handle, LED_PIN, 1);
            
            // Increased delay to 300ms to allow the ADS1115/optical sensor to fully boot up
            usleep(300000); 

            long sum_adc = 0;
            float sum_voltage = 0.0;
            int valid_reads = 0;

            // Take 5 readings (1 second apart)
            for (int r = 0; r < 5; r++) {
                int raw_adc = read_ads1115(i2c_handle);
                
                // Only add to average if it's not a complete I2C error
                if (raw_adc != 0 || r == 0) { 
                    float voltage = (raw_adc * 4.096) / 32768.0;
                    sum_adc += raw_adc;
                    sum_voltage += voltage;
                    valid_reads++;
                }
                sleep(1); 
            }

            // Turn OFF 12V and LED immediately after reading
            lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);
            lgGpioWrite(gpio_handle, LED_PIN, 0);

            // Calculate averages safely (prevents divide-by-zero if I2C completely failed)
            int avg_adc = (valid_reads > 0) ? (sum_adc / valid_reads) : 0;
            float avg_voltage = (valid_reads > 0) ? (sum_voltage / valid_reads) : 0.0;

            printf("     Spot %d Avg -> ADC: %d | Voltage: %.4f V\n", spot, avg_adc, avg_voltage);

            // Log averaged data directly to the CSV
            fprintf(log_file, "%d,%d,%d,%d,%.4f\n", cycle, spot, dark_current_raw, avg_adc, avg_voltage);
            fflush(log_file); // Ensure data writes immediately

            // Move to next spot (100 steps in dir 1) if not on the last spot
            if (spot < 8) {
                DRV8825_Turn(&motor1, 100, 1, 600);
                usleep(500000); 
            }
        }
        
        printf("Cycle %d complete.\n", cycle);
    }

    // 5. Clean up
    printf("\nAll 20 cycles complete. Cleaning up...\n");
    fclose(log_file);
    DRV8825_Stop(&motor1); 
    lgI2cClose(i2c_handle);
    lgGpiochipClose(gpio_handle);

    return 0;
}
