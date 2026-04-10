#!/usr/bin/env python3
import RPi.GPIO as GPIO
import time

# Motor 1: GPIO26=IN1, GPIO13=IN2, GPIO19=ENA
# Motor 2: GPIO16=IN3, GPIO20=IN4, GPIO21=ENB

GPIO.setmode(GPIO.BCM)
pins = [26, 13, 16, 20, 19, 21]
for p in pins:
    GPIO.setup(p, GPIO.OUT)

ena = GPIO.PWM(19, 1000)
enb = GPIO.PWM(21, 1000)
ena.start(0)
enb.start(0)

def both_forward(speed):
    GPIO.output(26, 1); GPIO.output(13, 0)
    GPIO.output(16, 1); GPIO.output(20, 0)
    ena.ChangeDutyCycle(speed)
    enb.ChangeDutyCycle(speed)

def both_stop():
    ena.ChangeDutyCycle(0)
    enb.ChangeDutyCycle(0)

def ramp(direction_fn):
    for s in range(0, 101, 5):
        direction_fn(s)
        print(f"speed: {s}%")
        time.sleep(0.05)
    time.sleep(1)
    for s in range(100, -1, -5):
        direction_fn(s)
        print(f"speed: {s}%")
        time.sleep(0.05)

try:
    while True:
        print("--- forward ---")
        ramp(both_forward)
        both_stop()
        time.sleep(1)
except KeyboardInterrupt:
    both_stop()
    GPIO.cleanup()
    print("stopped")
