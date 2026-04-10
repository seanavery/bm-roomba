#!/usr/bin/env python3
import pigpio
import time

# Motor 1: GPIO26=IN1, GPIO13=IN2, GPIO19=ENA
# Motor 2: GPIO16=IN3, GPIO20=IN4, GPIO21=ENB

PWM_FREQ = 1000  # 1kHz

pi = pigpio.pi()
if not pi.connected:
    print("pigpio not running - run: sudo pigpiod")
    exit()

def setup():
    for pin in [26, 13, 16, 20]:
        pi.set_mode(pin, pigpio.OUTPUT)
    pi.set_PWM_frequency(19, PWM_FREQ)
    pi.set_PWM_frequency(21, PWM_FREQ)

def both_forward(speed):
    # speed 0-255
    pi.write(26, 1)
    pi.write(13, 0)
    pi.write(16, 1)
    pi.write(20, 0)
    pi.set_PWM_dutycycle(19, speed)
    pi.set_PWM_dutycycle(21, speed)

def both_reverse(speed):
    pi.write(26, 0)
    pi.write(13, 1)
    pi.write(16, 0)
    pi.write(20, 1)
    pi.set_PWM_dutycycle(19, speed)
    pi.set_PWM_dutycycle(21, speed)

def both_stop():
    pi.set_PWM_dutycycle(19, 0)
    pi.set_PWM_dutycycle(21, 0)

def ramp(direction_fn, steps=25, delay=0.05):
    # ramp up
    for i in range(steps):
        speed = int((i / steps) * 255)
        direction_fn(speed)
        print(f"speed: {speed}")
        time.sleep(delay)
    # hold full speed
    direction_fn(255)
    time.sleep(1)
    # ramp down
    for i in range(steps, 0, -1):
        speed = int((i / steps) * 255)
        direction_fn(speed)
        print(f"speed: {speed}")
        time.sleep(delay)

try:
    setup()
    while True:
        print("--- forward ---")
        ramp(both_forward)
        both_stop()
        time.sleep(1)

        print("--- reverse ---")
        ramp(both_reverse)
        both_stop()
        time.sleep(1)

except KeyboardInterrupt:
    both_stop()
    pi.stop()
    print("stopped")
