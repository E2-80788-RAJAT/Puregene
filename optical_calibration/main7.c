#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <lgpio.h>

#define I2C_BUS "/dev/i2c-1"
#define ADS1115_ADDR 0x48 // Default I2C address when ADDR pin is tied to GND
#define LED_PIN 13
#define EN_12V_PIN 17

// ADS1115 Registers
#define POINTER_CONVERSION 0x00
#define POINTER_CONFIG 0x01

/* * Config Register Setup (16-bit):
 * OS[15]=1 (Single conversion), MUX[14:12]=100 (AIN0 to GND),
 * PGA[11:9]=001 (+/-4.096V), MODE[8]=1 (Single-shot),
 * DR[7:5]=100 (128 SPS), COMP[4:0]=00011 (Disable comparator)
 * Hex: 0xC383 
 */
#define CONFIG_REG_MSB 0xC3
#define CONFIG_REG_LSB 0x83

int init_ads1115() {
    int file = open(I2C_BUS, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        exit(1);
    }
    if (ioctl(file, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("Failed to connect to ADS1115");
        close(file);
        exit(1);
    }
    return file;
}

int read_ads1115(int file) {
    char write_buf[3];
    char read_buf[2];
    
    // Point to config register and write configuration
    write_buf[0] = POINTER_CONFIG;
    write_buf[1] = CONFIG_REG_MSB;
    write_buf[2] = CONFIG_REG_LSB;
    
    if (write(file, write_buf, 3) != 3) {
        perror("Failed to write to config register");
        return -1;
    }
    
    // Wait for the conversion to complete (1/128 SPS ≈ 8ms, waiting 10ms to be safe)
    usleep(10000);
    
    // Point to conversion register to read the value
    write_buf[0] = POINTER_CONVERSION;
    if (write(file, write_buf, 1) != 1) {
        perror("Failed to set pointer to conversion register");
        return -1;
    }
    
    if (read(file, read_buf, 2) != 2) {
        perror("Failed to read conversion register");
        return -1;
    }
    
    // Combine the two bytes (16-bit resolution)
    int val = (read_buf[0] << 8) | read_buf[1];
    
    // Handle negative numbers (two's complement for 16-bit)
    if (val > 0x7FFF) {
        val -= 0x10000;
    }
    
    return val;
}

int main() {
    int gpio_handle;
    int i2c_file;

    // 1. Initialize GPIO for Pi 5
    gpio_handle = lgGpiochipOpen(0); // Open GPIO chip 0
    if (gpio_handle < 0) {
        printf("Failed to open GPIO chip. Run with sudo?\n");
        return 1;
    }

    // Set pins as output and initialize them low
    lgGpioClaimOutput(gpio_handle, 0, EN_12V_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, LED_PIN, 0);

    // 2. Initialize I2C
    i2c_file = init_ads1115();

    // 3. Enable 12V supply
    printf("Enabling 12V Supply on GPIO %d...\n", EN_12V_PIN);
    lgGpioWrite(gpio_handle, EN_12V_PIN, 1);
    usleep(500000); // Give the power supply half a second to stabilize

    printf("Starting optical reading cycles (Press Ctrl+C to stop)...\n\n");

    // 4. Main Measurement Cycle
    while (1) {
        // --- Dark Current Calculation ---
        lgGpioWrite(gpio_handle, LED_PIN, 0); // Turn LED OFF
        usleep(20000); // Wait for LED to fully extinguish and sensor to settle
        
        int dark_val = read_ads1115(i2c_file);

        // --- Active Optical Reading ---
        lgGpioWrite(gpio_handle, LED_PIN, 1); // Turn LED ON
        usleep(20000); // Wait for LED to reach full brightness and sensor to settle
        
        int active_val = read_ads1115(i2c_file);

        if (dark_val != -1 && active_val != -1) {
            int compensated_val = active_val - dark_val;
            
            // Convert ADC value to voltage (Optional: based on +/- 4.096V PGA setting)
            // Voltage = (val * 4.096) / 32768.0
            
            printf("Dark Current: %6d | Active: %6d | Compensated Output: %6d\n", 
                   dark_val, active_val, compensated_val);
        }

        sleep(1); // 1-second delay between cycles
    }

    // Cleanup (though normally unreachable without a SIGINT handler)
    lgGpioWrite(gpio_handle, EN_12V_PIN, 0);
    lgGpioWrite(gpio_handle, LED_PIN, 0);
    lgGpiochipClose(gpio_handle);
    close(i2c_file);

    return 0;
}
