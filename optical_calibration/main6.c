#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include "drv8825.h"
#include <curl/curl.h>


// --- Pin Definitions ---
#define LIMIT_SWITCH_PIN 22
#define POWER_12V_PIN    17
#define LED_PIN          13 // Note: Conflicts with MOTOR1_DIR_PIN in original drv8825.h

// --- I2C / ADS1115 Definitions ---
#define I2C_BUS          1
#define ADS1115_ADDR     0x48
//#define VOLTAGE_MULTIPLIER 0.000125f // For +/- 4.096V range (4.096 / 32768)
#define VOLTAGE_MULTIPLIER 0.00003125f
#define SENDER_EMAIL "rajatsrivastavaaxon@gmail.com"
#define APP_PASSWORD "hbsbsbbnwjkpnlzk"



int send_email_log(){
	CURL *curl;
	CURLcode res = CURLE_OK;

	curl = curl_easy_init();

	if(curl){
		struct curl_slist *headers = NULL;
		struct curl_slist *recipients = NULL;
		curl_mime *mime;
		curl_mimepart *part;

		curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
		curl_easy_setopt(curl, CURLOPT_USERNAME, SENDER_EMAIL);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, APP_PASSWORD);
		curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

		curl_easy_setopt(curl, CURLOPT_MAIL_FROM, SENDER_EMAIL);
		recipients = curl_slist_append(recipients, "onlyunacademy@gmail.com");
		recipients = curl_slist_append(recipients, "59sonalisharma@gmail.com");
		curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

		headers = curl_slist_append(headers, "From: Raspberry Pi logger <" SENDER_EMAIL ">");
		headers = curl_slist_append(headers, "To:onlyunacademy@gmail.com, 59sonalisharma@gmail.com");
		headers = curl_slist_append(headers, "Subject: Hardware Run Complete: Optical Data Log");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		mime = curl_mime_init(curl);

		part = curl_mime_addpart(mime);
		curl_mime_data(part, "hello \n\n the data collection cycle has finished, please find the attached sheet\n\n Best regards,\nPuregene", CURL_ZERO_TERMINATED);
		part = curl_mime_addpart(mime);
		curl_mime_filedata(part, "optical_data_log.csv");
		curl_mime_type(part, "text/csv");
		curl_mime_name(part, "attachment");

		curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

		printf("\nConnecting to gmail via libcurl.......\n");
		res = curl_easy_perform(curl);

		if(res != CURLE_OK){
			fprintf(stderr, "Email failed to send: %s\n", curl_easy_strerror(res));

		}
		else{
			printf("Email sent successfully with attachment!\n");
		}

		curl_slist_free_all(recipients);
		curl_slist_free_all(headers);
		curl_mime_free(mime);
		curl_easy_cleanup(curl);



	}
	return (int)res;

}

// Function to read the ADS1115 on Channel A0
int read_ads1115(int i2c_handle) {
    // Config Register: 0x01
    // OS=1 (Start), MUX=100 (AIN0-GND), PGA=001 (+/- 4.096V), MODE=1 (Single-shot)
    // MSB = 11000001 (0xC1), LSB = 10000011 (0x83)
    char config_write[3] = {0x01, 0xC6, 0x08};
    lgI2cWriteDevice(i2c_handle, config_write, 3);
    
    // Wait for the conversion to complete (approx 8ms, giving it 15ms to be safe)
    usleep(15000); 

    // Point to Conversion Register: 0x00
    char reg_pointer[1] = {0x00};
    lgI2cWriteDevice(i2c_handle, reg_pointer, 1);
    
    // Read 2 bytes of data
    char data[2];
    lgI2cReadDevice(i2c_handle, data, 2);
    
    // Convert to 16-bit signed integer
    int val = (data[0] << 8) | data[1];
    if (val > 0x7FFF) {
        val -= 0x10000;
    }
    return val;
}

int main() {
	

	curl_global_init(CURL_GLOBAL_DEFAULT);

	
    // 1. Open GPIO and I2C chips
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle < 0) {
        printf("Error: Failed to open gpiochip.\n");
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
    
    lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN);
    lgGpioClaimOutput(gpio_handle, 0, POWER_12V_PIN, 0);
    lgGpioClaimOutput(gpio_handle, 0, LED_PIN, 0);

    // 3. Prepare CSV Log File
    FILE *log_file = fopen("optical_data_log.csv", "w");
    if (log_file == NULL) {
        printf("Error: Cannot create CSV file.\n");
        return -1;
    }
    // Write CSV Headers 
    fprintf(log_file, "Cycle,Spot,Dark_Current_Raw,Avg_ADC_Raw,Avg_Voltage_V\n");
    fflush(log_file);

    DRV8825_Enable(&motor1, 1); 
    usleep(500000); 

    // 4. Main Routine: 20 Cycles0.
    //
    

    for (int cycle = 1; cycle <=4; cycle++) {
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

        // B. Calculate Dark Current for this cycle (LED is OFF)
	lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
        int dark_current_raw = read_ads1115(i2c_handle);
        printf("Cycle %d Dark Current baseline: %d\n", cycle, dark_current_raw);

        // C. Move to First Spot (500 steps in dir 1)
        printf("Moving 500 steps to Spot 1...\n");
        DRV8825_Turn(&motor1, 20, 1, 600);
        sleep(1);

        // D. Iterate through 8 Spots
        for (int spot = 1; spot <= 5; spot++) {
            printf("  -> Taking readings at Spot %d...\n", spot);

            // Turn ON 12V and LED
           // lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
           // lgGpioWrite(gpio_handle, LED_PIN, 1);
	    
            usleep(100000); // Give sensors/light a moment to stabilize

            long sum_adc = 0;
            float sum_voltage = 0.0;
	    int raw_adc = 0;
	    float voltage = 0.0;

            // Take 5 readings over 5 seconds
            for (int r = 0; r < 5; r++) {
		lgGpioWrite(gpio_handle, LED_PIN, 1);
	    	usleep(150 * 1000);	
                raw_adc = read_ads1115(i2c_handle);
                voltage = raw_adc * VOLTAGE_MULTIPLIER;
                
                sum_adc += raw_adc;
                sum_voltage += voltage;
		lgGpioWrite(gpio_handle, LED_PIN, 0);
		usleep(150 * 1000);
                
                 // 1 reading per second
            }

            // Turn OFF 12V and LED
            // lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);
            //lgGpioWrite(gpio_handle, LED_PIN, 0);

            // Calculate averages
            int avg_adc = sum_adc / 5;
            float avg_voltage = sum_voltage / 5.0;

            printf("     Spot %d Avg -> ADC: %d | Voltage: %.4f V\n", spot, raw_adc, voltage);

            // Log averaged data (omitting the raw 80 samples for plotting efficiency)
            fprintf(log_file, "%d,%d,%d,%d,%.4f\n", cycle, spot, dark_current_raw, avg_adc, avg_voltage);
            fflush(log_file); // Ensure data is saved immediately

            // Move to next spot (100 steps in dir 1) if not on the last spot
            if (spot < 8) {
                DRV8825_Turn(&motor1, 225, 1, 600);
                usleep(500000); // Small pause between moves
            }
        }
	lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);
        
        printf("Cycle %d complete.\n", cycle);
    }
   // printf("homing for continous\n");
   // lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 0);
  //  while(lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN)==1){
//	    lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1);
//	    usleep(600);
 //   lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0);
//	    usleep(600);
   // }
   // sleep(1);
   // DRV8825_Turn(&motor1, 675, 1, 600);
   // lgGpioWrite(gpio_handle, LED_PIN, 1);
   // lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
  //  for(int r = 0; r < 20; r++){
//	    lgGpioWrite(gpio_handle, LED_PIN, 1);
//	    int raw_adc1 = read_ads1115(i2c_handle);
//	    float voltage1 = raw_adc1 * VOLTAGE_MULTIPLIER;
//	    lgGpioWrite(gpio_handle, LED_PIN, 0);
//	    printf("ADC : %d | Voltage: %.4f V \n", raw_adc1, voltage1);
//	    sleep(1);
//	
  // }
    

    // 5. Clean up
    printf("\nAll 20 cycles complete. Cleaning up...\nData collection complete. Cleaning up hardware.....\n");

    fclose(log_file);
    DRV8825_Stop(&motor1); 
    lgI2cClose(i2c_handle);
    lgGpiochipClose(gpio_handle);
    send_email_log();
    curl_global_cleanup();


    return 0;
}


