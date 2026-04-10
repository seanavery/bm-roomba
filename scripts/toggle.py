#!/usr/bin/env python3
import subprocess
import time

def pins_forward():
    subprocess.run("pinctrl set 21 op dh && pinctrl set 16 op dh && pinctrl set 20 op dl", shell=True)

def pins_stop():
    subprocess.run("pinctrl set 21 op dl", shell=True)

try:
    while True:
        print("forward")
        pins_forward()
        time.sleep(2)
        print("stop")
        pins_stop()
        time.sleep(2)
except KeyboardInterrupt:
    pins_stop()
    print("stopped")
