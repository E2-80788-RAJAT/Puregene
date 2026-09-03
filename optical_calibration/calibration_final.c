#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include <curl/curl.h>
#include "drv8825.h"

#define LIMIT_SWITCH_PIN 22
#define POWER_12V_PIN 17
#define LED_PIN 13

#define I2C_BUS 1
#define ADS1115_ADDR 0x48
#define VOLTAGE_MULTIPLIER 0.000125f

#define SENDER_EMAIL "rajatsrivastavaaxon@gmail.com"
#define APP_PASSWORD ""

