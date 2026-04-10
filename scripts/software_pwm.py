#!/usr/bin/env python3
from gpiozero import Motor, Device
from gpiozero.pins.lgpio import LGPIOFactory
import time

Device.pin_factory = LGPIOFactory()

# Motor(forward, backward, enable)
motor1 = Motor(forward=26, backward=13, enable=19, pwm=True)
motor2 = Motor(forward=16, backward=20, enable=21, pwm=True)

try:
    while True:
        print("forward - ramping up")
        for speed in [i/10 for i in range(0, 11)]:
            motor1.forward(speed)
            motor2.forward(speed)
            print(f"speed: {speed:.1f}")
            time.sleep(0.1)
        time.sleep(1)

        print("stop")
        motor1.stop()
        motor2.stop()
        time.sleep(1)

        print("reverse - ramping up")
        for speed in [i/10 for i in range(0, 11)]:
            motor1.backward(speed)
            motor2.backward(speed)
            print(f"speed: {speed:.1f}")
            time.sleep(0.1)
        time.sleep(1)

        print("stop")
        motor1.stop()
        motor2.stop()
        time.sleep(1)

except KeyboardInterrupt:
    motor1.stop()
    motor2.stop()
    print("stopped")
