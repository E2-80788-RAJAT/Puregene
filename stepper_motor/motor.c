#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<lgpio.h>

#define M1_DIR 13
#define M1_STEP 19
#define M1_EN 12

#define ENABLE_MOTOR 0
#define DISABLE_MOTOR 1

int main(int argc, char *argv[]){
	if(argc != 3){
		printf("Usage : %s <steps> <direction (1 = forward, 0 = backward)>\n", argv[0]);
		printf("Example : sudo %s 200 1\n", argv[0]);
		return -1;

	}

	int steps = atoi(argv[1]);
	int direction = atoi(argv[2]);

	int delay_us = 2000;

	int h = lgGpiochipOpen(0);
	if(h < 0){
		printf("Error : Failed to open GPIO. Run with sudo.\n");
		return -1;

	}

	lgGpioClaimOutput(h, 0, M1_STEP, 0);
	lgGpioClaimOutput(h, 0, M1_DIR, direction);

	lgGpioClaimOutput(h, 0, M1_EN, DISABLE_MOTOR);
	usleep(10000);
	lgGpioWrite(h, M1_EN, ENABLE_MOTOR);

	printf("Exectuing %d steps. Watch the STEP pin toggle....\n", steps);
	for(int i = 0; i < steps; i++){
		lgGpioWrite(h, M1_STEP, 1);
		usleep(delay_us / 2);
		lgGpioWrite(h, M1_STEP, 0);
		usleep(delay_us / 2);

	}


	lgGpioWrite(h, M1_EN, DISABLE_MOTOR);
	lgGpiochipClose(h);

	printf("Motion Complete.\n");

	return 0;
}
