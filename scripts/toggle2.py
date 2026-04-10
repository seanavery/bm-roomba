#!/usr/bin/env python3
import subprocess
import time

# Motor 1: GPIO26=IN1, GPIO13=IN2, GPIO19=ENA
# Motor 2: GPIO16=IN3, GPIO20=IN4, GPIO21=ENB

def both_forward():
    subprocess.run("pinctrl set 19 op dh && pinctrl set 26 op dh && pinctrl set 13 op dl", shell=True)
    subprocess.run("pinctrl set 21 op dh && pinctrl set 16 op dh && pinctrl set 20 op dl", shell=True)

def both_stop():
    subprocess.run("pinctrl set 19 op dl && pinctrl set 21 op dl", shell=True)

try:
    while True:
        print("forward")
        both_forward()
        time.sleep(2)
        print("stop")
        both_stop()
        time.sleep(2)
except KeyboardInterrupt:
    both_stop()
    print("stopped")
