import asyncio
import os
import sys
sys.path.append('/home/viam/.local/lib/python3.11/site-packages/')

from dotenv import load_dotenv
from evdev import InputDevice, ecodes
from viam.robot.client import RobotClient
from viam.components.base import Base
from viam.proto.common import Vector3

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

DEVICE_PATH = '/dev/input/event5'

API_KEY    = os.environ['VIAM_API_KEY']
API_KEY_ID = os.environ['VIAM_API_KEY_ID']
ROBOT_ADDR = os.environ['VIAM_ROBOT_ADDR']

DEADZONE   = 0.15
HZ         = 20


def normalize(val: int) -> float:
    """Map DS4 stick axis (0–255, center=128) to [-1.0, 1.0]."""
    n = (val - 128) / 128.0
    return 0.0 if abs(n) < DEADZONE else n


class GamepadDriver:
    def __init__(self):
        self.device  = InputDevice(DEVICE_PATH)
        self.linear  = 0.0  # left stick Y
        self.angular = 0.0  # left stick X

    def process_event(self, event) -> bool:
        """Update axis state. Returns True if stop button pressed."""
        if event.type == ecodes.EV_ABS:
            if event.code == ecodes.ABS_Y:
                # invert Y: stick up (low val) = forward
                self.linear = -normalize(event.value)
            elif event.code == ecodes.ABS_X:
                # invert X: stick left (low val) = turn left
                self.angular = -normalize(event.value)
        elif event.type == ecodes.EV_KEY:
            # Cross button (BTN_SOUTH) = emergency stop
            if event.code == ecodes.BTN_SOUTH and event.value == 1:
                self.linear  = 0.0
                self.angular = 0.0
                return True
            # Options button (BTN_START) = exit
            if event.code == ecodes.BTN_START and event.value == 1:
                raise SystemExit("Options pressed — exiting")
        return False


async def connect():
    opts = RobotClient.Options.with_api_key(
        api_key=API_KEY,
        api_key_id=API_KEY_ID,
    )
    return await RobotClient.at_address(ROBOT_ADDR, opts)


async def control_loop(base: Base, gamepad: GamepadDriver):
    interval = 1.0 / HZ
    print(f"Control loop running at {HZ}Hz — left stick to drive, X to stop, Options to exit")

    while True:
        # drain all pending events
        try:
            for event in gamepad.device.read():
                stopped = gamepad.process_event(event)
                if stopped:
                    await base.stop()
                    print("Emergency stop")
        except BlockingIOError:
            pass  # no events this tick

        await base.set_power(
            linear=Vector3(x=0, y=gamepad.linear,  z=0),
            angular=Vector3(x=0, y=0, z=gamepad.angular),
        )
        await asyncio.sleep(interval)


async def main():
    gamepad = GamepadDriver()
    print(f"Gamepad: {gamepad.device.name}")

    async with await connect() as machine:
        base = Base.from_robot(machine, "base")
        print("Connected to robot")
        try:
            await control_loop(base, gamepad)
        except SystemExit as e:
            print(e)
        finally:
            await base.stop()


if __name__ == '__main__':
    asyncio.run(main())
