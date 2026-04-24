import asyncio
import math
from typing import Any, ClassVar, Mapping, Optional, Sequence

from gpiozero import Device, Motor
from gpiozero.pins.lgpio import LGPIOFactory
from viam.components.base import Base
from viam.proto.app.robot import ComponentConfig
from viam.proto.common import ResourceName, Vector3
from viam.resource.base import ResourceBase
from viam.resource.registry import Registry, ResourceCreatorRegistration
from viam.resource.types import Model, ModelFamily

Device.pin_factory = LGPIOFactory()

# L298N pin assignments (BCM)
LEFT_FORWARD  = 13
LEFT_BACKWARD = 26
LEFT_ENABLE   = 19
RIGHT_FORWARD  = 16
RIGHT_BACKWARD = 20
RIGHT_ENABLE   = 21


class RoombaPiBase(Base):
    MODEL: ClassVar[Model] = Model(ModelFamily("sean", "roomba-pi"), "base")

    # Config defaults
    DEFAULT_WIDTH_MM: int             = 235
    DEFAULT_WHEEL_CIRCUMFERENCE_MM: int = 220
    DEFAULT_MAX_SPEED_MM_S: float     = 1341.0  # 3 mph
    DEFAULT_MAX_SPIN_DEG_S: float     = 180.0   # lower than linear — spinning has more friction

    @classmethod
    def new(
        cls,
        config: ComponentConfig,
        dependencies: Mapping[ResourceName, ResourceBase],
    ) -> "RoombaPiBase":
        base = cls(config.name)
        base.reconfigure(config, dependencies)
        return base

    @classmethod
    def validate_config(cls, config: ComponentConfig) -> Sequence[str]:
        return [], []

    def reconfigure(
        self,
        config: ComponentConfig,
        dependencies: Mapping[ResourceName, ResourceBase],
    ) -> None:
        if hasattr(self, "motor_left"):
            self.motor_left.close()
            self.motor_right.close()

        def attr(name: str, default):
            fields = config.attributes.fields
            return type(default)(fields[name].number_value) if name in fields else default

        self.width_mm              = attr("width_mm",              self.DEFAULT_WIDTH_MM)
        self.wheel_circumference_mm = attr("wheel_circumference_mm", self.DEFAULT_WHEEL_CIRCUMFERENCE_MM)
        self.max_speed_mm_s        = attr("max_speed_mm_s",        self.DEFAULT_MAX_SPEED_MM_S)
        self.max_spin_deg_s        = attr("max_spin_deg_s",        self.DEFAULT_MAX_SPIN_DEG_S)

        self.motor_left  = Motor(forward=LEFT_FORWARD,  backward=LEFT_BACKWARD,  enable=LEFT_ENABLE,  pwm=True)
        self.motor_right = Motor(forward=RIGHT_FORWARD, backward=RIGHT_BACKWARD, enable=RIGHT_ENABLE, pwm=True)
        self._moving: bool = False

    # ------------------------------------------------------------------ #
    # Internal helpers                                                     #
    # ------------------------------------------------------------------ #

    def _clamp(self, value: float, lo: float = -1.0, hi: float = 1.0) -> float:
        return max(lo, min(hi, value))

    def _set_motors(self, left: float, right: float) -> None:
        self.motor_left.value  = self._clamp(left)
        self.motor_right.value = self._clamp(right)
        self._moving = left != 0.0 or right != 0.0

    # ------------------------------------------------------------------ #
    # Viam Base API                                                        #
    # ------------------------------------------------------------------ #

    async def move_straight(
        self,
        distance: int,
        velocity: float,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        print(f"~~~~~~~~~~~~~~~~~~~~~~~~~moving straight dist={distance} velo={velocity}")
        if distance == 0 or velocity == 0:
            await self.stop()
            return

        speed = abs(velocity) / self.max_speed_mm_s
        power = self._clamp(speed if distance > 0 else -speed)
        duration = abs(distance / velocity)

        self._set_motors(power, power)
        await asyncio.sleep(duration)
        await self.stop()

    async def spin(
        self,
        angle: float,
        velocity: float,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        print(f"~~~~~~~~~~~~~~~~~~~~~~~~~spin angle={angle} velo={velocity}")
        if angle == 0 or velocity == 0:
            await self.stop()
            return

        power    = self._clamp(abs(velocity) / self.max_spin_deg_s, 0.0, 1.0)
        duration = abs(angle / velocity)

        # Positive angle = CCW (left): left wheel back, right wheel forward
        if angle > 0:
            self._set_motors(-power, power)
        else:
            self._set_motors(power, -power)

        await asyncio.sleep(duration)
        await self.stop()

    async def set_power(
        self,
        linear: Vector3,
        angular: Vector3,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        # linear.y: +1 = full forward, -1 = full reverse
        # angular.z: +1 = full left, -1 = full right
        print(f"~~~~~~~~~~~~~~~~~~~~~~~~~set power linear={linear} angular={angular}")
        self._set_motors(
            self._clamp(linear.y - angular.z),
            self._clamp(linear.y + angular.z),
        )

    async def set_velocity(
        self,
        linear: Vector3,
        angular: Vector3,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        print(f"~~~~~~~~~~~~~~~~~~~~~~~~~set velocity linear={linear} angular={angular}")
        # linear.y in mm/s, angular.z in deg/s
        omega = angular.z * math.pi / 180.0
        self._set_motors(
            self._clamp((linear.y - omega * self.width_mm / 2.0) / self.max_speed_mm_s),
            self._clamp((linear.y + omega * self.width_mm / 2.0) / self.max_speed_mm_s),
        )

    async def stop(
        self,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        self._set_motors(0, 0)

    async def is_moving(self) -> bool:
        return self._moving

    async def get_properties(
        self,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> Base.Properties:
        return Base.Properties(
            width_meters=self.width_mm / 1000.0,
            turning_radius_meters=0.0,
            wheel_circumference_meters=self.wheel_circumference_mm / 1000.0,
        )

    async def do_command(
        self,
        command: Mapping[str, Any],
        *,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> Mapping[str, Any]:
        raise NotImplementedError(f"unknown command: {command}")

    async def close(self) -> None:
        self._set_motors(0, 0)
        self.motor_left.close()
        self.motor_right.close()


Registry.register_resource_creator(
    Base.API,
    RoombaPiBase.MODEL,
    ResourceCreatorRegistration(RoombaPiBase.new, RoombaPiBase.validate_config),
)


if __name__ == "__main__":
    async def _test():
        cfg = ComponentConfig(name="test")
        b = RoombaPiBase.new(cfg, {})
        try:
            print("move_straight: 300mm @ 300mm/s (forward)")
            await b.move_straight(300, 300)
            await asyncio.sleep(0.5)

            print("move_straight: -300mm @ 300mm/s (reverse)")
            await b.move_straight(-300, 300)
            await asyncio.sleep(0.5)

            print("spin: +90° @ 90°/s (CCW / left)")
            await b.spin(90, 90)
            await asyncio.sleep(0.5)

            print("spin: -90° @ 90°/s (CW / right)")
            await b.spin(-90, 90)
            await asyncio.sleep(0.5)

            print("set_power: y=0.5 for 1s")
            await b.set_power(Vector3(x=0, y=0.5, z=0), Vector3(x=0, y=0, z=0))
            await asyncio.sleep(1)
            await b.stop()
            await asyncio.sleep(0.5)

            print("set_velocity: y=300mm/s, angular.z=45°/s for 1s (arc left)")
            await b.set_velocity(Vector3(x=0, y=300, z=0), Vector3(x=0, y=0, z=45))
            await asyncio.sleep(1)
            await b.stop()

            print(f"is_moving={await b.is_moving()}")
            print(f"properties={await b.get_properties()}")
        finally:
            await b.close()

    asyncio.run(_test())
