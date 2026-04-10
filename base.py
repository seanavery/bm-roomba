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


class RoombaPiBase(Base):
    MODEL: ClassVar[Model] = Model(ModelFamily("sean", "roomba-pi"), "base")

    # --- defaults ---
    DEFAULT_WIDTH_MM: int = 235
    DEFAULT_WHEEL_CIRCUMFERENCE_MM: int = 220
    DEFAULT_MAX_SPEED_MM_S: float = 1341.0  # 3 mph
    DEFAULT_MAX_SPIN_DEG_S: float = 180.0   # tunable — spinning has more friction than driving straight

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
        return []

    def reconfigure(
        self,
        config: ComponentConfig,
        dependencies: Mapping[ResourceName, ResourceBase],
    ) -> None:
        # Close existing motors before re-initializing on config reload
        if hasattr(self, "motor_left"):
            self.motor_left.close()
            self.motor_right.close()

        attrs = config.attributes.fields

        self.width_mm: int = int(
            attrs["width_mm"].number_value
            if "width_mm" in attrs
            else self.DEFAULT_WIDTH_MM
        )
        self.wheel_circumference_mm: int = int(
            attrs["wheel_circumference_mm"].number_value
            if "wheel_circumference_mm" in attrs
            else self.DEFAULT_WHEEL_CIRCUMFERENCE_MM
        )
        self.max_speed_mm_s: float = float(
            attrs["max_speed_mm_s"].number_value
            if "max_speed_mm_s" in attrs
            else self.DEFAULT_MAX_SPEED_MM_S
        )
        self.max_spin_deg_s: float = float(
            attrs["max_spin_deg_s"].number_value
            if "max_spin_deg_s" in attrs
            else self.DEFAULT_MAX_SPIN_DEG_S
        )

        # Motor 1 = left:  IN1=GPIO26 (fwd), IN2=GPIO13 (bwd), ENA=GPIO19 (PWM)
        # Motor 2 = right: IN3=GPIO16 (fwd), IN4=GPIO20 (bwd), ENB=GPIO21 (PWM)
        self.motor_left = Motor(forward=26, backward=13, enable=19, pwm=True)
        self.motor_right = Motor(forward=16, backward=20, enable=21, pwm=True)
        self._moving: bool = False

    # ------------------------------------------------------------------ #
    # Internal helpers                                                     #
    # ------------------------------------------------------------------ #

    def _clamp(self, value: float, lo: float = -1.0, hi: float = 1.0) -> float:
        return max(lo, min(hi, value))

    def _set_motors(self, left: float, right: float) -> None:
        left = self._clamp(left)
        right = self._clamp(right)
        self.motor_left.value = left
        self.motor_right.value = right
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
        if angle == 0 or velocity == 0:
            await self.stop()
            return

        power    = self._clamp(abs(velocity) / self.max_spin_deg_s, 0.0, 1.0)
        duration = abs(angle / velocity)

        # Positive angle = CCW = left turn: left wheel back, right wheel forward
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
        left  = self._clamp(linear.y - angular.z)
        right = self._clamp(linear.y + angular.z)
        self._set_motors(left, right)

    async def set_velocity(
        self,
        linear: Vector3,
        angular: Vector3,
        *,
        extra: Optional[Mapping[str, Any]] = None,
        timeout: Optional[float] = None,
        **kwargs,
    ) -> None:
        # linear.y in mm/s, angular.z in deg/s
        omega = angular.z * math.pi / 180.0  # deg/s → rad/s
        left_mm_s  = linear.y - (omega * self.width_mm / 2.0)
        right_mm_s = linear.y + (omega * self.width_mm / 2.0)
        left  = self._clamp(left_mm_s  / self.max_speed_mm_s)
        right = self._clamp(right_mm_s / self.max_speed_mm_s)
        self._set_motors(left, right)

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
            turning_radius_meters=0.0,  # differential drive can turn in place
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
