#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<lgpio.h>

#define M1_DIR 13
#define M1_STEP 19
#define M1_EN 12

#define M2_DIR 24
#define M2_STEP 18
#define M2_EN 4

#define ENABLE_MOTOR 0
#define DISABLE_MOTOR 1

void move_steps(int gpio_handle, int step_pin, int dir_pin, int en_pin, int steps, int direction, int delay_us){
	lgGpioWrite(gpio_handle, en_pin, ENABLE_MOTOR);

	lgGpioWrite(gpio_handle, dir_pin, direction);

	for(int i = 0; i<steps; i++){
		lgGpioWrite(gpio_handle, step_pin, 1);
		usleep(delay_us/2);
		lgGpioWrite(gpio_handle, step_pin, 0);
		usleep(delay_us/2);
	}

	lgGpioWrite(gpio_handle, en_pin, DISABLE_MOTOR);

}

int main(int argc, char *argv[]){
	if(argc != 4){
		printf("Usage : %s <motor_id (1 or 2)> <steps> <direction(1=forward, 0=backward)>\n", argv[0]);
		return -1;

	}

	int motor_id = atoi(argv[1]);
	int steps = atoi(argv[2]);
	int direction = atoi(argv[3]);

	int step_pin, dir_pin, en_pin;
	
	if(motor_id == 1){
		step_pin = M1_STEP; 
		dir_pin = M1_DIR;
		en_pin = M1_EN;
	}
	else if(motor_id == 2){
		step_pin = M2_STEP;
		dir_pin = M2_DIR;
		en_pin = M2_EN;

	}
	else{
		printf("Error: Invalid motor ID. Use 1 or 2.\n");
		return -1;

	}

	int h = lgGpiochipOpen(0);
	if(h < 0){
		printf("Failed to open GPIO chip. Ensure you are running with sudo.\n");
		return -1;

	}

	lgGpioClaimOutput(h, 0, dir_pin, 0);
	lgGpioClaimOutput(h, 0, step_pin, 0);
	lgGpioClaimOutput(h, 0, en_pin, DISABLE_MOTOR);

	printf("Moving motor %d for %d steps......\n", motor_id, steps);

	move_steps(h, step_pin, dir_pin, en_pin, steps, direction, 2000);

	lgGpiochipClose(h);

	return 0;



}
