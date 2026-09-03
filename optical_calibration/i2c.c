#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stdint.h>

#define I2C_BUS "/dev/i2c-1"
#define ADS1115_ADDR 0x48

// Thermistor Parameters
#define B_COEFFICIENT 3950.0       // Standard Beta coefficient for 100k NTC
#define NOMINAL_RESISTANCE 100000.0 // 100k ohms at 25C
#define NOMINAL_TEMPERATURE 298.15  // 25C in Kelvin
#define SERIES_RESISTOR 100000.0    // 100k fixed resistor in the voltage divider
#define V_IN 5                    // Input voltage powering the divider

int main() {
    int file;
    
    // 1. Open the I2C bus
    if ((file = open(I2C_BUS, O_RDWR)) < 0) {
        perror("Failed to open the I2C bus");
        return 1;
    }

    // 2. Connect to the ADS1115 slave device
    if (ioctl(file, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("Failed to acquire bus access and/or talk to slave");
        close(file);
        return 1;
    }

    // 3. Configure the ADS1115
    // OS=1 (Start single conversion)
    // MUX=100 (AIN0 to GND)
    // PGA=001 (+/- 4.096V range - good for 3.3V measurements)
    // MODE=1 (Single-shot mode)
    // DR=100 (128 samples per second)
    // COMP=0000011 (Disable comparator)
    // Hex: 0x8000 | 0x4000 | 0x0200 | 0x0100 | 0x0080 | 0x0003 = 0xC383
    uint8_t config[3] = {0x01, 0xC3, 0x83}; // Register 0x01 is the Config register
    if (write(file, config, 3) != 3) {
        perror("Failed to write config register");
        close(file);
        return 1;
    }

    // Wait for the conversion to complete (1/128s is ~8ms, we sleep 20ms to be safe)
    usleep(20000);

    // 4. Set pointer back to Conversion register (0x00) to read the data
    uint8_t reg[1] = {0x00};
    if (write(file, reg, 1) != 1) {
        perror("Failed to set pointer to conversion register");
        close(file);
        return 1;
    }

    // 5. Read the 2-byte conversion result
    uint8_t data[2];
    if (read(file, data, 2) != 2) {
        perror("Failed to read conversion results");
        close(file);
        return 1;
    }

    // 6. Convert the data to a 16-bit signed integer
    int16_t val = (data[0] << 8) | data[1];

    // 7. Calculate Voltage
    // PGA is set to +/- 4.096V max. Max signed 16-bit integer is 32767.
    float voltage = (val * 4.096) / 32767.0;

    // Safety check to prevent divide-by-zero if reading goes wrong
    if (voltage >= V_IN || voltage <= 0) {
        printf("Error: Voltage reading out of logical bounds (%.3f V).\n", voltage);
        close(file);
        return 1;
    }

    // 8. Calculate Thermistor Resistance
    // V_out = V_in * (R_ntc / (R_series + R_ntc))
    float resistance = (voltage * SERIES_RESISTOR) / (V_IN - voltage);

    // 9. Calculate Temperature using the Beta Equation
    float steinhart;
    steinhart = resistance / NOMINAL_RESISTANCE;      // (R/Ro)
    steinhart = log(steinhart);                       // ln(R/Ro)
    steinhart /= B_COEFFICIENT;                       // 1/B * ln(R/Ro)
    steinhart += 1.0 / NOMINAL_TEMPERATURE;           // + (1/To)
    steinhart = 1.0 / steinhart;                      // Invert
    steinhart -= 273.15;                              // Convert Kelvin to Celsius

    // Output results
    printf("--- Sensor Reading ---\n");
    printf("Raw ADC Value : %d\n", val);
    printf("Voltage       : %.3f V\n", voltage);
    printf("Resistance    : %.0f Ohms\n", resistance);
    printf("Temperature   : %.2f °C\n", steinhart);

    close(file);
    return 0;
}
