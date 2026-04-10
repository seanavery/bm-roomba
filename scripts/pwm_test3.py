#!/usr/bin/env python3
from rpi_hardware_pwm import HardwarePWM
import subprocess
import time

# Motor 1: GPIO26=IN1, GPIO13=IN2, GPIO19=ENA (hwpwm channel 0)
# Motor 2: GPIO16=IN3, GPIO20=IN4, GPIO21=ENB (hwpwm channel 1)

def pin(p, state):
    subprocess.run(f"pinctrl set {p} op {'dh' if state else 'dl'}", shell=True)

ena = HardwarePWM(pwm_channel=0, hz=1000, chip=2)
enb = HardwarePWM(pwm_channel=1, hz=1000, chip=2)
ena.start(0)
enb.start(0)

def both_forward(speed):
    pin(26, 1); pin(13, 0)
    pin(16, 1); pin(20, 0)
    ena.change_duty_cycle(speed)
    enb.change_duty_cycle(speed)

def both_stop():
    ena.change_duty_cycle(0)
    enb.change_duty_cycle(0)

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
    ena.stop()
    enb.stop()
    print("stopped")
