#include <stdio.h>
#include <unistd.h>
#include <lgpio.h>
#include "drv8825.h"


int main(){
	int handle = lgGpiochipOpen(0);
	if(handle<0){
		printf("Failed to open GPIO, Run with sudo.\n");
		return -1;

	}


	StepperMotor motor1;

	DRV8825_Init(&motor1, handle, MOTOR1_DIR_PIN, MOTOR1_STEP_PIN, MOTOR1_ENABLE_PIN);
	printf("Enabling Motor.....\n");

	DRV8825_Enable(&motor1, 1);
	sleep(2);

	printf("Moving forward....\n");

	DRV8825_Turn(&motor1, 500, 1, 600);

	sleep(1);

	printf("MOving backward\n");

	DRV8825_Turn(&motor1, 500, 0, 600);

	printf("Disabling Motor....\n");

	DRV8825_Stop(&motor1);
	lgGpiochipClose(handle);

	return 0;
}
