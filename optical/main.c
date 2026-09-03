#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <pigpio.h>

// ---------------------------------------------------------
// GPIO Pin Configuration (Using BCM Pin Numbering)
// UPDATE THESE NUMBERS TO MATCH YOUR RASPBERRY PI WIRING
// ---------------------------------------------------------
#define PIN_12V_EN 17 // GPIO pin connected to the 12V Enable
#define PIN_LED_EN 27 // GPIO pin connected to the LED CTRL/PWM

// ADS1115 I2C Address (ADDR pin is grounded in the schematic)
#define ADS1115_ADDR 0x48

// ADS1115 Register Pointers
#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

int main() {
    // Initialize the pigpio library
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Failed to initialize pigpio. Try running with sudo.\n");
        return 1;
    }

    // ==========================================
    // 1. GPIO Setup & Power Sequencing
    // ==========================================
    gpioSetMode(PIN_12V_EN, PI_OUTPUT);
    gpioSetMode(PIN_LED_EN, PI_OUTPUT);

    // Enable 12V supply
    gpioWrite(PIN_12V_EN, 1);
    printf("12V Supply Enabled.\n");
    time_sleep(0.5); // 500ms delay to allow power rails to stabilize

    // Turn on the LED
    gpioWrite(PIN_LED_EN, 1);
    printf("LED Enabled.\n");
    time_sleep(0.1); // 100ms delay to allow the OPT101 sensor to react to light

    // ==========================================
    // 2. I2C Setup & ADC Reading
    // ==========================================
    // Open I2C bus 1 (standard for RPi 4) at the ADS1115 address
    int i2c_handle = i2cOpen(1, ADS1115_ADDR, 0); 
    if (i2c_handle < 0) {
        fprintf(stderr, "Failed to open I2C bus.\n");
        gpioWrite(PIN_LED_EN, 0);
        gpioWrite(PIN_12V_EN, 0);
        gpioTerminate();
        return 1;
    }

    // Configure the ADS1115 Register
    // We want: Single-ended AIN0, +/- 4.096V range (PGA), Single-shot mode
    // MSB: OS(1) | MUX(100 for AIN0) | PGA(001 for 4.096V) | MODE(1 for single-shot) = 0xC3
    // LSB: DR(100 for 128SPS) | COMP_MODE(0) | COMP_POL(0) | COMP_LAT(0) | COMP_QUE(11) = 0x83
    uint8_t config[3];
    config[0] = REG_CONFIG;
    config[1] = 0xC3; // MSB
    config[2] = 0x83; // LSB

    if (i2cWriteDevice(i2c_handle, (char*)config, 3) != 0) {
        fprintf(stderr, "Failed to write configuration to ADS1115.\n");
        i2cClose(i2c_handle);
        gpioTerminate();
        return 1;
    }

    // Wait for the conversion to complete (128 SPS takes ~8ms; waiting 20ms to be safe)
    time_sleep(0.02);

    // Set the pointer to the Conversion Register to read the data
    i2cWriteByte(i2c_handle, REG_CONVERSION);
    int raw_data = i2cReadWordData(i2c_handle, REG_CONVERSION);

    if (raw_data < 0) {
        fprintf(stderr, "Failed to read data from ADS1115.\n");
    } else {
        // The ADS1115 sends data in Big-Endian format, but I2C functions 
        // often read as Little-Endian. We need to swap the bytes.
        int16_t raw_adc = (raw_data >> 8) | ((raw_data & 0xFF) << 8);

        // Calculate voltage
        // The ADS1115 is a 16-bit signed integer (-32768 to 32767).
        // With PGA set to +/- 4.096V, the formula is: (raw * 4.096) / 32768
        float voltage = (raw_adc * 4.096) / 32768.0;

        printf("---------------------------\n");
        printf("Raw ADC Value : %d\n", raw_adc);
        printf("Optical Voltage: %.4f V\n", voltage);
        printf("---------------------------\n");
    }

    // ==========================================
    // 3. Cleanup & Shutdown
    // ==========================================
    // Turn off the LED and the 12V supply to save power
    gpioWrite(PIN_LED_EN, 0);
    printf("LED Disabled.\n");

    gpioWrite(PIN_12V_EN, 0);
    printf("12V Supply Disabled.\n");

    // Close connections
    i2cClose(i2c_handle);
    gpioTerminate();

    return 0;
}
