#include "HardwareController.h"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <time.h>
#include <lgpio.h>
#include <curl/curl.h>
#include <QDebug>

extern "C" {
#include "drv8825.h"
}

// Hardware Constants
#define LIMIT_SWITCH_PIN   6
#define LIMIT_SWITCH_2_PIN 5
#define POWER_12V_PIN      17
#define LED_PIN            13

#define MOTOR2_DIR_PIN     24
#define MOTOR2_STEP_PIN    18
#define MOTOR2_ENABLE_PIN  4

#define I2C_BUS          1
#define ADS1115_ADDR     0x48
#define VOLTAGE_MULTIPLIER 0.00003125f

#define SENDER_EMAIL "rajatsrivastavaaxon@gmail.com"
#define APP_PASSWORD "hbsbsbbnwjkpnlzk"

// Globals for C-logic
int gpio_handle = -1;
int i2c_handle = -1;
int serial_fd = -1;
StepperMotor motor1;
StepperMotor motor2;

std::atomic<bool> opt_running{false};
std::atomic<bool> opt_stop_req{false};
int target_cycles_global = 60;

float temp_data[4] = {0.0, 0.0, 0.0, 0.0};
pthread_mutex_t temp_mutex = PTHREAD_MUTEX_INITIALIZER;
HardwareController* global_instance = nullptr;

// --- Helper Functions ---
static int send_email_log(const char* filename) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    curl = curl_easy_init();
    if(curl) {
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
        headers = curl_slist_append(headers, "To: onlyunacademy@gmail.com, 59sonalisharma@gmail.com");
        headers = curl_slist_append(headers, "Subject: Hardware Run Complete: Optical Data Log");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        mime = curl_mime_init(curl);
        part = curl_mime_addpart(mime);
        curl_mime_data(part, "Hello,\n\nThe data collection cycle has finished.\n\nBest regards,\nPuregene", CURL_ZERO_TERMINATED);
        part = curl_mime_addpart(mime);
        curl_mime_filedata(part, filename);
        curl_mime_type(part, "text/csv");
        curl_mime_name(part, "attachment");
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        res = curl_easy_perform(curl);
        curl_slist_free_all(recipients);
        curl_slist_free_all(headers);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
    }
    return (int)res;
}

static int read_ads1115(int i2c_h) {
    char config_write[3] = {0x01, (char)0xC6, 0x08};
    lgI2cWriteDevice(i2c_h, config_write, 3);
    usleep(15000);
    char reg_pointer[1] = {0x00};
    lgI2cWriteDevice(i2c_h, reg_pointer, 1);
    char data[2];
    lgI2cReadDevice(i2c_h, data, 2);
    int val = (data[0] << 8) | data[1];
    if (val > 0x7FFF) val -= 0x10000;
    return val;
}

static void optical_home() {
    lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 1);
    while (lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN) == 1 && !opt_stop_req) {
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1); usleep(600);
        lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0); usleep(600);
    }
    usleep(200000);
}

// --- Class Implementation ---
HardwareController::HardwareController(QObject *parent) : QObject(parent) {
    global_instance = this;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    gpio_handle = lgGpiochipOpen(0);
    i2c_handle = lgI2cOpen(I2C_BUS, ADS1115_ADDR, 0);

    if (gpio_handle >= 0) {
        DRV8825_Init(&motor1, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
        lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN);

        DRV8825_Init(&motor2, gpio_handle, MOTOR2_DIR_PIN, MOTOR2_STEP_PIN, MOTOR2_ENABLE_PIN);
        lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_2_PIN);

        lgGpioClaimOutput(gpio_handle, 0, POWER_12V_PIN, 0);
        lgGpioClaimOutput(gpio_handle, 0, LED_PIN, 0);
        lgGpioWrite(gpio_handle, LED_PIN, 0);
    }

    serial_fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd != -1) {
        struct termios options;
        tcgetattr(serial_fd, &options);
        cfsetispeed(&options, B115200);
        cfsetospeed(&options, B115200);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tcsetattr(serial_fd, TCSANOW, &options);

        pthread_t tid;
        pthread_create(&tid, NULL, serialReaderThread, this);
    }
}

HardwareController::~HardwareController() {
    if (serial_fd != -1) close(serial_fd);
    if (gpio_handle >= 0) lgGpiochipClose(gpio_handle);
    if (i2c_handle >= 0) lgI2cClose(i2c_handle);
    curl_global_cleanup();
}

float HardwareController::topTemp() const { return m_topTemp; }
float HardwareController::bottomTemp() const { return m_bottomTemp; }

void HardwareController::setTopTemp(float temp) {
    if(serial_fd != -1) { char buf[32]; snprintf(buf, sizeof(buf), "set_top:%.1f\n", temp); write(serial_fd, buf, strlen(buf)); }
}
void HardwareController::setBottomTemp(float temp) {
    if(serial_fd != -1) { char buf[32]; snprintf(buf, sizeof(buf), "set_bot:%.1f\n", temp); write(serial_fd, buf, strlen(buf)); }
}
void HardwareController::startHeaters() {
    if(serial_fd != -1) write(serial_fd, "start heaters\n", 14);
}
void HardwareController::stopHeaters() {
    if(serial_fd != -1) write(serial_fd, "stop heaters\n", 13);
}

void HardwareController::trayIn() {
    if (gpio_handle >= 0) {
        DRV8825_Enable(&motor2, 1);
        lgGpioWrite(gpio_handle, MOTOR2_DIR_PIN, 0);
        while (lgGpioRead(gpio_handle, LIMIT_SWITCH_2_PIN) == 1) {
            lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 1); usleep(600);
            lgGpioWrite(gpio_handle, MOTOR2_STEP_PIN, 0); usleep(600);
        }
        DRV8825_Stop(&motor2);
    }
}

void HardwareController::trayOut() {
    if (gpio_handle >= 0) {
        DRV8825_Enable(&motor2, 1);
        DRV8825_Turn(&motor2, 3000, 1, 600);
        DRV8825_Stop(&motor2);
    }
}

void HardwareController::startOptical(int targetCycles) {
    if (!opt_running) {
        target_cycles_global = targetCycles;
        pthread_t tid;
        pthread_create(&tid, NULL, opticalThreadFunc, this);
    }
}

void HardwareController::stopOptical() {
    if(opt_running) opt_stop_req = true;
}

void* HardwareController::serialReaderThread(void* arg) {
    HardwareController* self = static_cast<HardwareController*>(arg);
    char read_buf[256]; char line_buf[512]; int line_len = 0;

    while(1) {
        int n = read(serial_fd, read_buf, sizeof(read_buf)-1);
        if (n > 0) {
            read_buf[n] = '\0';
            for(int i = 0; i < n; i++) {
                if (read_buf[i] == '\n' || read_buf[i] == '\r') {
                    if (line_len > 0) {
                        line_buf[line_len] = '\0';
                        char *ptr = strstr(line_buf, "A2:");
                        if (ptr) {
                            float t0, t1, t2, t3;
                            // Pull all 4 matching Arduino String
                            if (sscanf(line_buf, "A0: %fC | A1: %fC | A2: %fC | A3: %fC", &t0, &t1, &t2, &t3) == 4) {
                                pthread_mutex_lock(&temp_mutex);
                                temp_data[0] = t0; temp_data[1] = t1;
                                temp_data[2] = t2; temp_data[3] = t3;
                                pthread_mutex_unlock(&temp_mutex);

                                self->m_topTemp = t2;
                                self->m_bottomTemp = t3;
                                emit self->tempsUpdated();
                            }
                        }
                        line_len = 0;
                    }
                } else if (line_len < sizeof(line_buf)-1) {
                    line_buf[line_len++] = read_buf[i];
                }
            }
        }
        usleep(50000);
    }
    return NULL;
}

void* HardwareController::opticalThreadFunc(void* arg) {
    HardwareController* self = static_cast<HardwareController*>(arg);
    opt_running = true;
    opt_stop_req = false;
    lgGpioWrite(gpio_handle, LED_PIN, 0);

    char filename[256];
    int run_num = 1;
    while(1) {
        snprintf(filename, sizeof(filename), "puregene_run_%d.csv", run_num);
        if (access(filename, F_OK) != 0) break;
        run_num++;
    }

    FILE *log_file = fopen(filename, "w");
    if (log_file) {
        fprintf(log_file, "Cycle,Spot,Dark_Current_Raw,Avg_ADC_Raw,Avg_Voltage_V,Temp_A0,Temp_A1,Temp_A2,Temp_A3\n");
        fflush(log_file);
    }

    DRV8825_Enable(&motor1, 1);
    usleep(500000);

    struct timespec cycle_target_time;
    struct timespec now;

    for (int cycle = 1; cycle <= target_cycles_global; cycle++) {
        if(opt_stop_req) break;

        optical_home();
        if(opt_stop_req) break;

        if (cycle == 1) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            cycle_target_time = now;
            cycle_target_time.tv_sec += 20;
        }

        DRV8825_Turn(&motor1, 1310, 0, 500);
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 1);
        usleep(100000);
        int dark_current_raw = read_ads1115(i2c_handle);

        optical_home();
        if(opt_stop_req) break;

        DRV8825_Turn(&motor1, 65, 0, 500);

        for (int spot = 1; spot <= 6; spot++) {
            if(opt_stop_req) break;
            usleep(100000);

            long sum_adc = 0; float sum_voltage = 0.0;
            int raw_adc = 0;  float voltage = 0.0;

            for (int r = 0; r < 5; r++) {
                if(opt_stop_req) break;
                lgGpioWrite(gpio_handle, LED_PIN, 1);
                usleep(150 * 1000);
                raw_adc = read_ads1115(i2c_handle);
                voltage = raw_adc * VOLTAGE_MULTIPLIER;
                sum_adc += raw_adc; sum_voltage += voltage;
                lgGpioWrite(gpio_handle, LED_PIN, 0);
                usleep(150 * 1000);
            }

            lgGpioWrite(gpio_handle, LED_PIN, 0);
            if(opt_stop_req) break;

            int avg_adc = sum_adc / 5;
            float avg_voltage = sum_voltage / 5.0;

            // Bridge Hardware Data to QML Chart
            emit self->graphDataUpdated(spot, cycle, (float)avg_adc);

            pthread_mutex_lock(&temp_mutex);
            float t0 = temp_data[0]; float t1 = temp_data[1]; float t2 = temp_data[2]; float t3 = temp_data[3];
            pthread_mutex_unlock(&temp_mutex);

            if(log_file) {
                fprintf(log_file, "%d,%d,%d,%d,%.4f,%.2f,%.2f,%.2f,%.2f\n",
                        cycle, spot, dark_current_raw, avg_adc, avg_voltage, t0, t1, t2, t3);
                fflush(log_file);
            }

            if (spot < 6) {
                DRV8825_Turn(&motor1, 165.5, 0, 500);
                usleep(100000);
            }
        }

        lgGpioWrite(gpio_handle, LED_PIN, 0);
        lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);

        if (!opt_stop_req) {
            optical_home();

            clock_gettime(CLOCK_MONOTONIC, &now);
            long diff_ms = (cycle_target_time.tv_sec - now.tv_sec) * 1000 +
                           (cycle_target_time.tv_nsec - now.tv_nsec) / 1000000;

            if (diff_ms > 0) {
                while (!opt_stop_req) {
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    long remain = (cycle_target_time.tv_sec - now.tv_sec) * 1000 +
                                  (cycle_target_time.tv_nsec - now.tv_nsec) / 1000000;
                    if (remain <= 0) break;
                    usleep((remain > 100) ? 50000 : remain * 1000);
                }
            }
            cycle_target_time.tv_sec += 20;
        }
    }

    lgGpioWrite(gpio_handle, LED_PIN, 0);
    lgGpioWrite(gpio_handle, POWER_12V_PIN, 0);
    if(log_file) fclose(log_file);
    DRV8825_Stop(&motor1);

    send_email_log(filename);
    emit self->runComplete("Run Complete! Email sent successfully.");

    opt_running = false;
    return NULL;
}

