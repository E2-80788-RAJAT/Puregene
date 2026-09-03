import RPi.GPIO as GPIO
import time

DIR_PIN = 13
STEP_PIN = 19
ENABLE_PIN = 12

def setup_motor():
    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)
    
    GPIO.setup(DIR_PIN, GPIO.OUT)
    GPIO.setup(STEP_PIN, GPIO.OUT)
    
