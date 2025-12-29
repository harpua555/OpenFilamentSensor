#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#

from typing import Any, Optional, Tuple

from construct import Int16ul, Int32ul, Padding, Struct

from . import BaseArchMethodsMixin, BaseTargetMethods, ESPCoreDumpLoaderError

RISCV_GP_REGS_COUNT = 32
PRSTATUS_SIZE = 204
PRSTATUS_OFFSET_PR_CURSIG = 12
PRSTATUS_OFFSET_PR_PID = 24
PRSTATUS_OFFSET_PR_REG = 72
ELF_GREGSET_T_SIZE = 128

PrStruct = Struct(
    Padding(PRSTATUS_OFFSET_PR_CURSIG),
    'pr_cursig' / Int16ul,
    Padding(PRSTATUS_OFFSET_PR_PID - PRSTATUS_OFFSET_PR_CURSIG - Int16ul.sizeof()),
    'pr_pid' / Int32ul,
    Padding(PRSTATUS_OFFSET_PR_REG - PRSTATUS_OFFSET_PR_PID - Int32ul.sizeof()),
    'regs' / Int32ul[RISCV_GP_REGS_COUNT],
    Padding(PRSTATUS_SIZE - PRSTATUS_OFFSET_PR_REG - ELF_GREGSET_T_SIZE)
)


class RiscvMethodsMixin(BaseArchMethodsMixin):
    @staticmethod
    def get_registers_from_stack(data, grows_down):
        # type: (bytes, bool) -> Tuple[list[int], Optional[dict[int, int]]]
        regs = Int32ul[RISCV_GP_REGS_COUNT].parse(data)
        if not grows_down:
            raise ESPCoreDumpLoaderError('Growing up stacks are not supported for now!')
        return regs, None

    @staticmethod
    def build_prstatus_data(tcb_addr, task_regs):  # type: (int, list[int]) -> Any
        return PrStruct.build({
            'pr_cursig': 0,
            'pr_pid': tcb_addr,
            'regs': task_regs,
        })


class Esp32C3Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32c3'


class Esp32C2Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32c2'


class Esp32H2Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32h2'


class Esp32C6Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32c6'


class Esp32P4Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32p4'


class Esp32C5Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32c5'


class Esp32C61Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32c61'


class Esp32H21Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32h21'


class Esp32H4Methods(BaseTargetMethods, RiscvMethodsMixin):
    TARGET = 'esp32h4'
