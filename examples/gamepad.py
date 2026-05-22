import asyncio
import os
import sys
sys.path.append('/home/viam/.local/lib/python3.11/site-packages/')

from dotenv import load_dotenv
from evdev import InputDevice, ecodes, list_devices
from viam.robot.client import RobotClient
from viam.components.base import Base
from viam.components.motor import Motor
from viam.proto.common import Vector3

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

API_KEY    = os.environ['VIAM_API_KEY']
API_KEY_ID = os.environ['VIAM_API_KEY_ID']
ROBOT_ADDR = os.environ['VIAM_ROBOT_ADDR']

DEADZONE   = 0.15
HZ         = 20


def find_gamepad() -> InputDevice:
    """Scan /dev/input/event* for a PS4 controller's main gamepad device.
    The PS4 exposes several sub-devices sharing the same name prefix
    (touchpad, motion sensors); pick the one that's *just* the gamepad.
    """
    excluded = ('Touchpad', 'Motion Sensors')
    for path in list_devices():
        try:
            d = InputDevice(path)
        except OSError:
            continue
        name = d.name
        if (('Wireless Controller' in name or 'DualShock' in name)
                and not any(s in name for s in excluded)):
            return d
        d.close()
    raise RuntimeError("No gamepad detected — is it powered on and connected?")


def normalize(val: int) -> float:
    """Map DS4 stick axis (0–255, center=128) to [-1.0, 1.0]."""
    n = (val - 128) / 128.0
    return 0.0 if abs(n) < DEADZONE else n


def normalize_trigger(val: int) -> float:
    """Map DS4 trigger axis (0=released, 255=fully pressed) to [0.0, 1.0]."""
    n = val / 255.0
    return 0.0 if n < 0.05 else n


class GamepadDriver:
    def __init__(self):
        self.device  = find_gamepad()
        self.linear  = 0.0  # left stick Y
        self.angular = 0.0  # left stick X
        self.vacuum  = 0.0  # L2 analog forward (0..1), L1 digital reverse (-1)
        self.brush   = 0.0  # R2 analog forward (0..1), R1 digital reverse (-1)
        self._l1_held = False
        self._l2_power = 0.0  # 0.0 (released) .. 1.0 (fully pressed)
        self._r1_held = False
        self._r2_power = 0.0  # 0.0 (released) .. 1.0 (fully pressed)

    def _update_vacuum(self):
        if self._l2_power > 0 and not self._l1_held:
            self.vacuum = self._l2_power
        elif self._l1_held and self._l2_power == 0:
            self.vacuum = -1.0
        else:
            self.vacuum = 0.0

    def _update_brush(self):
        if self._r2_power > 0 and not self._r1_held:
            self.brush = self._r2_power
        elif self._r1_held and self._r2_power == 0:
            self.brush = -1.0
        else:
            self.brush = 0.0

    def process_event(self, event) -> bool:
        """Update axis state. Returns True if stop button pressed."""
        if event.type == ecodes.EV_ABS:
            if event.code == ecodes.ABS_Y:
                # invert Y: stick up (low val) = forward
                self.linear = -normalize(event.value)
            elif event.code == ecodes.ABS_X:
                # invert X: stick left (low val) = turn left
                self.angular = -normalize(event.value)
            elif event.code == ecodes.ABS_Z:
                # L2 analog trigger — proportional vacuum forward power
                self._l2_power = normalize_trigger(event.value)
                self._update_vacuum()
            elif event.code == ecodes.ABS_RZ:
                # R2 analog trigger — proportional brush forward power
                self._r2_power = normalize_trigger(event.value)
                self._update_brush()
        elif event.type == ecodes.EV_KEY:
            # Cross button (BTN_SOUTH) = emergency stop
            if event.code == ecodes.BTN_SOUTH and event.value == 1:
                self.linear  = 0.0
                self.angular = 0.0
                self.vacuum  = 0.0
                self.brush   = 0.0
                self._l1_held = False
                self._l2_power = 0.0
                self._r1_held = False
                self._r2_power = 0.0
                return True
            # Options button (BTN_START) = exit
            if event.code == ecodes.BTN_START and event.value == 1:
                raise SystemExit("Options pressed — exiting")
            # L1 (BTN_TL) = vacuum full reverse while held (digital — no pressure sensor)
            if event.code == ecodes.BTN_TL:
                self._l1_held = (event.value == 1)
                self._update_vacuum()
            # R1 (BTN_TR) = brush full reverse while held (digital — no pressure sensor)
            elif event.code == ecodes.BTN_TR:
                self._r1_held = (event.value == 1)
                self._update_brush()
        return False


async def connect():
    opts = RobotClient.Options.with_api_key(
        api_key=API_KEY,
        api_key_id=API_KEY_ID,
    )
    return await RobotClient.at_address(ROBOT_ADDR, opts)


async def control_loop(base: Base, vacuum: Motor, brush: Motor, gamepad: GamepadDriver):
    interval = 1.0 / HZ
    print(f"Control loop running at {HZ}Hz — left stick to drive, "
          "L2/L1 for vacuum fwd/rev, R2/R1 for brush fwd/rev, X to stop, Options to exit")

    while True:
        # drain all pending events
        try:
            for event in gamepad.device.read():
                stopped = gamepad.process_event(event)
                if stopped:
                    await base.stop()
                    await vacuum.stop()
                    await brush.stop()
                    print("Emergency stop")
        except BlockingIOError:
            pass  # no events this tick

        await base.set_power(
            linear=Vector3(x=0, y=gamepad.linear,  z=0),
            angular=Vector3(x=0, y=0, z=gamepad.angular),
        )
        await vacuum.set_power(gamepad.vacuum)
        await brush.set_power(gamepad.brush)
        await asyncio.sleep(interval)


async def main():
    gamepad = GamepadDriver()
    print(f"Gamepad: {gamepad.device.name}")

    async with await connect() as machine:
        base   = Base.from_robot(machine, "base")
        vacuum = Motor.from_robot(machine, "vacuum")
        brush  = Motor.from_robot(machine, "brush")
        print("Connected to robot")
        try:
            await control_loop(base, vacuum, brush, gamepad)
        except SystemExit as e:
            print(e)
        finally:
            await base.stop()
            await vacuum.stop()
            await brush.stop()


if __name__ == '__main__':
    asyncio.run(main())
