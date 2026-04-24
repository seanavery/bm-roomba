import asyncio

import sys
sys.path.append('/home/viam/.local/lib/python3.11/site-packages/')

from viam.module.module import Module
from viam.components.base import Base

# Importing base.py triggers the Registry.register_resource_creator call
import base as roomba_base


async def main():
    module = Module.from_args()
    module.add_model_from_registry(Base.API, roomba_base.RoombaPiBase.MODEL)
    await module.start()


if __name__ == "__main__":
    asyncio.run(main())
