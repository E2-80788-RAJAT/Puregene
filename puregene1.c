#include<stdio.h>
#include<lgpio.h>
#include "drv8825.h"
#include<unistd.h>


#define LIMIT_SWITCH_PIN_TRAY 5
#define LIMIT_SWITCH_PIN_OPTICAL 6


int main(void){
	int gpio_handle = lgGpiochipOpen(0);
	StepperMotor motor1;
	StepperMotor motor2;

	DRV8825_Init(&motor1, gpio_handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
	lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, LIMIT_SWITCH_PIN_TRAY);
	DRV8825_Enable(&motor1, 1);
	usleep(500000);

	printf("Homing motor.....\n");
	lgGpioWrite(gpio_handle, MOTOR1_DIR_PIN, 0);
	while(lgGpioRead(gpio_handle, LIMIT_SWITCH_PIN_TRAY) == 1){
		lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 1);
		usleep(600);
		lgGpioWrite(gpio_handle, MOTOR1_STEP_PIN, 0);
		usleep(600);

	}
	sleep(1);
	DRV8825_Turn(&motor1, 3150, 1, 600);

	return 0;
}
