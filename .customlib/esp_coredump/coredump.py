#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# ESP-IDF Core Dump Utility
#
import os
import subprocess
import sys
import textwrap
from contextlib import contextmanager
from shutil import copyfile, which
from typing import Optional, Tuple, Union

import serial

from .tools import load_json_from_file

try:
    # esptool>=4.0
    from esptool.cmds import detect_chip
    from esptool.loader import ESPLoader
except (AttributeError, ModuleNotFoundError):
    # esptool<4.0
    from esptool import ESPLoader
    detect_chip = ESPLoader.detect_chip

from construct import Container, GreedyRange, Int32ul, ListContainer, Struct

from .corefile import RISCV_TARGETS, SUPPORTED_TARGETS, XTENSA_TARGETS, xtensa
from .corefile.elf import (TASK_STATUS_CORRECT, ElfFile, ElfSegment,
                           ESPCoreDumpElfFile, EspTaskStatus)
from .corefile.gdb import DEFAULT_GDB_TIMEOUT_SEC, EspGDB
from .corefile.loader import (ESPCoreDumpFileLoader, ESPCoreDumpFlashLoader,
                              ESPCoreDumpLoaderError, EspCoreDumpVersion,
                              get_core_file_format)

IDF_PATH = os.getenv('IDF_PATH', '')
ESP_ROM_ELF_DIR = os.getenv('ESP_ROM_ELF_DIR')
# 'tools/idf_py_actions/roms.json' is used for compatibility with ESP-IDF before v5.5, when the file was moved
ROMS_JSON = [os.path.join(IDF_PATH, 'components', 'esp_rom', 'roms.json'), os.path.join(IDF_PATH, 'tools', 'idf_py_actions', 'roms.json')]

MORE_INFO_MSG = 'Read more: https://github.com/espressif/esp-coredump/blob/master/README.md#installation'
GDB_NOT_FOUND_ERROR = (
    f'GDB executable not found. Please install GDB or set up ESP-IDF to complete the action. '
    f'{MORE_INFO_MSG}'
)
IDF_SETUP_ERROR = f'Please set up ESP-IDF to complete the action. {MORE_INFO_MSG}'

RETRY_ATTEMPTS = 3

XTENSA_ISR_CTX_IDX = 37
RISCV_ISR_CTX_IDX = 1

if os.name == 'nt':
    CLOSE_FDS = False
else:
    CLOSE_FDS = True


class CoreDump:
    def __init__(self,
                 baud: Optional[int] = int(os.environ.get('ESPTOOL_BAUD', ESPLoader.ESP_ROM_BAUD)),
                 chip: str = os.environ.get('ESPTOOL_CHIP', 'auto'),
                 core_format: str = 'auto',
                 port: Optional[str] = os.environ.get('ESPTOOL_PORT'),
                 gdb_timeout_sec: int = DEFAULT_GDB_TIMEOUT_SEC,
                 core: Optional[str] = None,
                 chip_rev: Optional[int] = None,
                 gdb: Optional[str] = None,
                 extra_gdbinit_file: Optional[str] = None,
                 off: Optional[int] = None,
                 parttable_off: Optional[int] = None,
                 prog: Optional[str] = None,
                 print_mem: Optional[str] = None,
                 rom_elf: Optional[str] = None,
                 save_core: Optional[str] = None,
                 ):
        if prog is None:
            raise ValueError("Path to program\'s ELF binary is not provided")

        self.baud = baud
        self.chip = chip
        self.core = core
        self.chip_rev = chip_rev
        self.core_format = get_core_file_format(core) if core and core_format == 'auto' else core_format
        self.gdb = gdb
        self.gdb_timeout_sec = gdb_timeout_sec
        self.extra_gdbinit_file = extra_gdbinit_file
        self.coredump_off = off
        self.parttable_off = parttable_off
        self.prog = prog
        self.port = port
        self.print_mem = print_mem
        self.rom_elf = rom_elf
        self.save_core = save_core

    @staticmethod
    def load_aux_elf(elf_path):  # type: (str) -> str
        """
        Loads auxiliary ELF file and composes GDB command to read its symbols.
        """
        sym_cmd = ''
        if os.path.exists(elf_path):
            elf = ElfFile(elf_path)
            if os.name == 'nt':
                elf_path = elf_path.replace('\\', '/')
            for s in elf.sections:
                if s.name == '.text':
                    sym_cmd = f'add-symbol-file {elf_path} {s.addr:#x}'
                    break
        return sym_cmd

    def extract_chip_rev_from_elf(self):
        if not os.path.exists(self.core):
            raise FileNotFoundError(f"Provided ELF file {self.core} is not found or doesn't exist")
        elf = ElfFile(elf_path=self.core)
        chip_rev = None
        for s in elf.note_segments:
            for n in s.note_secs:
                if b'ESP_CHIP_REV' in n.name:
                    chip_rev = int.from_bytes(n.desc, 'little')
                    break

        return chip_rev

    def get_core_header_info_dict(self, e_machine=ESPCoreDumpElfFile.EM_XTENSA):
        loader = None  # type: Union[ESPCoreDumpFlashLoader, ESPCoreDumpFileLoader, None]
        core_dump_info_map = {
            'core_elf_path': None,
            'target': None,
            'temp_files': None,
            'chip_rev': None,
        }

        if not self.core:
            # Core file not specified, try to read core dump from flash.
            if not IDF_PATH:
                print(IDF_SETUP_ERROR)
                sys.exit(1)
            loader = ESPCoreDumpFlashLoader(self.coredump_off, port=self.port, baud=self.baud, part_table_offset=self.parttable_off)
        elif self.core_format != 'elf':
            # Core file specified, but not yet in ELF format. Convert it from raw or base64 into ELF.
            loader = ESPCoreDumpFileLoader(self.core, self.core_format == 'b64')
        else:
            # Core file is already in the ELF format
            core_dump_info_map['core_elf_path'] = self.core
            chip_rev = self.extract_chip_rev_from_elf()

            if self.chip_rev is not None and chip_rev != self.chip_rev:
                print('Provided chip revision does not match the one extracted from the provided coredump elf file.',
                      file=sys.stderr)
                exit(1)

            core_dump_info_map['chip_rev'] = chip_rev

        # Load/convert the core file
        if loader:
            loader.create_corefile(exe_name=self.prog, e_machine=e_machine)
            core_dump_info_map['core_elf_path'] = loader.core_elf_file
            if self.save_core:
                # We got asked to save the core file, make a copy
                copyfile(loader.core_elf_file, self.save_core)
            core_dump_info_map['target'] = loader.target
            core_dump_info_map['chip_rev'] = loader.chip_rev
            core_dump_info_map['temp_files'] = loader.temp_files

        return core_dump_info_map

    def get_chip_version(self):  # type: () -> Optional[int]
        for segment in self.core_elf.note_segments:
            for sec in segment.note_secs:
                if sec.type == ESPCoreDumpElfFile.PT_ESP_INFO:
                    ver_bytes = sec.desc[:4]
                    return int((ver_bytes[3] << 8) | ver_bytes[2])
        return None

    def get_target(self):  # type: () -> str

        if self.chip != 'auto':
            return self.chip  # type: ignore

        chip_version = self.get_chip_version()
        if chip_version is not None:
            if chip_version == EspCoreDumpVersion.ESP32:
                return 'esp32'

            if chip_version == EspCoreDumpVersion.ESP32S2:
                return 'esp32s2'

            if chip_version == EspCoreDumpVersion.ESP32S3:
                return 'esp32s3'

            if chip_version == EspCoreDumpVersion.ESP32C3:
                return 'esp32c3'

            if chip_version == EspCoreDumpVersion.ESP32C2:
                return 'esp32c2'

            if chip_version == EspCoreDumpVersion.ESP32C6:
                return 'esp32c6'

            if chip_version == EspCoreDumpVersion.ESP32H2:
                return 'esp32h2'

            if chip_version == EspCoreDumpVersion.ESP32P4:
                return 'esp32p4'

            if chip_version == EspCoreDumpVersion.ESP32C5:
                return 'esp32c5'

            if chip_version == EspCoreDumpVersion.ESP32C61:
                return 'esp32c61'

        target = None
        try:
            inst = detect_chip(self.port, self.baud)
        except serial.serialutil.SerialException:
            print('Unable to identify the chip type. '
                  'Please use the --chip option to specify the chip type or '
                  'connect the board and provide the --port option to have the chip type determined automatically.',
                  file=sys.stderr)
            exit(1)
        else:
            target = inst.CHIP_NAME.lower().replace('-', '')

        return target  # type: ignore

    def get_gdb_path(self, target):  # type: (str) -> Optional[str]
        if self.gdb:
            return self.gdb  # type: ignore

        if target in XTENSA_TARGETS:
            # For some reason, xtensa-esp32s2-elf-gdb will report some issue.
            # Use xtensa-esp32-elf-gdb instead.
            gdb_path = 'xtensa-esp32-elf-gdb'
        elif target in RISCV_TARGETS:
            gdb_path = 'riscv32-esp-elf-gdb'
        else:
            raise ValueError(f'Invalid value: {target}. For now we only support {SUPPORTED_TARGETS}')
        if which(gdb_path) is None:
            return None

        return gdb_path

    def get_gdb_args(self, target, core_elf_path, chip_rev, is_dbg_mode=False):
        gdb_tool = self.get_gdb_path(target)
        if not gdb_tool:
            print(GDB_NOT_FOUND_ERROR)
            sys.exit(1)

        rom_elf_path = self.get_rom_elf_path(target=target, chip_rev=chip_rev)
        rom_sym_cmd = self.load_aux_elf(rom_elf_path)

        gdb_args = [gdb_tool]

        if self.extra_gdbinit_file:
            if not os.path.isfile(self.extra_gdbinit_file):
                raise ValueError(f'{self.extra_gdbinit_file} does not exist')
            gdb_args.append('-x={}'.format(self.extra_gdbinit_file))
        else:
            gdb_args.append('--nx')  # ignore .gdbinit

        if not is_dbg_mode:
            gdb_args.append('--nw')  # suppress the GUI interface
            gdb_args.append('--quiet')  # inhibit dumping info at start-up
            gdb_args.append('--interpreter=mi2')  # use GDB/MI v2

        gdb_args.append('--core={}'.format(core_elf_path))  # core file
        if rom_sym_cmd:
            gdb_args += ['-ex', rom_sym_cmd]
        gdb_args.append(self.prog)

        return gdb_args

    def get_rom_elf_path(self, chip_rev, target):  # type: (Optional[int], str) -> str
        if self.rom_elf:
            return self.rom_elf

        if chip_rev is None:
            return ''

        rom_file_path = ''

        if not IDF_PATH or not ESP_ROM_ELF_DIR:
            print("The ROM ELF file won't be loaded automatically since you are running the utility out of IDF.")
            return rom_file_path

        target_roms = None
        for rom_json_path in ROMS_JSON:
            try:
                roms_json = load_json_from_file(rom_json_path)
                target_roms = roms_json.get(target, [])
                break
            except FileNotFoundError:
                continue

        if not target_roms:
            print("The ROM ELF file won't load automatically since it was not found for the provided chip type.")
            return rom_file_path

        index = len(target_roms)
        for idx in range(len(target_roms)):
            if target_roms[idx].get('rev') == chip_rev:
                index = idx
                break
        if index < len(target_roms):
            chip_rev_from_json = target_roms[index]['rev']
            rom_elf_file_name = f'{target}_rev{chip_rev_from_json}_rom.elf'
            rom_file_path = os.path.join(ESP_ROM_ELF_DIR, rom_elf_file_name)
        else:
            print("The ROM ELF file won't load automatically since it was not found for the provided chip type.")

        return rom_file_path

    def get_task_info_extra_note_tuple(self):  # type: () -> Tuple[Optional[list[str]], Optional[Container]]
        extra_note = None
        task_info = []
        for note_seg in self.core_elf.note_segments:
            for note_sec in note_seg.note_secs:
                if note_sec.type == ESPCoreDumpElfFile.PT_ESP_EXTRA_INFO:
                    extra_note = note_sec
                if note_sec.type == ESPCoreDumpElfFile.PT_ESP_TASK_INFO:
                    task_info_struct = EspTaskStatus.parse(note_sec.desc)
                    task_info.append(task_info_struct)
        return task_info, extra_note

    def get_panic_details(self):
        for note_seg in self.core_elf.note_segments:
            for note_sec in note_seg.note_secs:
                if note_sec.type == ESPCoreDumpElfFile.PT_ESP_PANIC_DETAILS:
                    return note_sec
        return None

    def print_crashed_task_info(self, marker):  # type: (Optional[int]) -> None
        if marker == ESPCoreDumpElfFile.CURR_TASK_MARKER:
            print('\nCrashed task has been skipped.')
        else:
            task_name = self.gdb_esp.get_freertos_task_name(marker)
            print("\nCrashed task handle: 0x%x, name: '%s', GDB name: 'process %d'"
                  % (marker, task_name, marker))  # type: ignore

    def print_threads_info(self, task_info):  # type: (Optional[list[Container]]) -> None
        print(self.gdb_esp.run_cmd('info threads'))
        # THREADS STACKS
        for attempt in range(1, RETRY_ATTEMPTS + 1):
            threads, _ = self.gdb_esp.get_thread_info(attempt * DEFAULT_GDB_TIMEOUT_SEC)
            if threads:
                break

            print('Retrying reading threads information...')

        if not threads:
            print('\nThe threads information for the current task could not be retrieved. '
                  'Please try running this command again with --gdb-timeout-sec option to increase '
                  f'the default value of internal delay for gdb responses (Default: {DEFAULT_GDB_TIMEOUT_SEC})')
            return

        print('\n')
        print('       TCB             NAME PRIO C/B  STACK USED/FREE')
        print('---------- ---------------- -------- ----------------')

        thread_dict = {}
        for thr in threads:
            thr_id = int(thr['id'])
            tcb_addr = self.gdb_esp.gdb2freertos_thread_id(thr['target-id'])
            task_name = self.gdb_esp.get_freertos_task_name(tcb_addr)
            try:
                pxEndOfStack = int(self.gdb_esp.parse_tcb_variable(tcb_addr, 'pxEndOfStack'), 16)
                pxTopOfStack = int(self.gdb_esp.parse_tcb_variable(tcb_addr, 'pxTopOfStack'), 16)
                pxStack = int(self.gdb_esp.parse_tcb_variable(tcb_addr, 'pxStack'), 16)
                uxPriority = int(self.gdb_esp.parse_tcb_variable(tcb_addr, 'uxPriority'), 16)
                uxBasePriority = int(self.gdb_esp.parse_tcb_variable(tcb_addr, 'uxBasePriority'), 16)
            except ValueError:
                pxEndOfStack = pxTopOfStack = pxStack = uxPriority = uxBasePriority = 0

            thread_dict[thr_id] = {'tcb_addr': tcb_addr, 'task_name': task_name}
            ftcb_addr = '0x{:x}'.format(tcb_addr)
            if pxStack == 0:
                print(f'{ftcb_addr:>10} Corrupted TCB data')
            else:
                fpriority = '{}/{}'.format(uxPriority, uxBasePriority)
                fstack_usage = '{}/{}'.format(abs(pxEndOfStack - pxTopOfStack), abs(pxStack - pxTopOfStack))
                print(f'{ftcb_addr:>10}{task_name:>17}{fpriority:>9}{fstack_usage:>17}')

        for thr_id, value in thread_dict.items():
            tcb_addr = value['tcb_addr']
            task_index = thr_id - 1
            task_name = value['task_name']
            self.gdb_esp.switch_thread(thr_id)
            print('\n==================== THREAD {} (TCB: 0x{:x}, name: \'{}\') ====================='
                  .format(thr_id, tcb_addr, task_name))

            print(self.gdb_esp.run_cmd('bt'))
            if task_info and task_info[task_index].task_flags != TASK_STATUS_CORRECT:
                print("The task '%s' is corrupted." % thr_id)
                print('Task #%d info: flags, tcb, stack (%x, %x, %x).' % (task_info[task_index].task_index,
                                                                          task_info[task_index].task_flags,
                                                                          task_info[task_index].task_tcb_addr,
                                                                          task_info[task_index].task_stack_start))

    def print_current_thread_registers(self, extra_note, extra_info):
        # type: (Optional[Container], Optional[ListContainer]) -> None
        if self.exe_elf.e_machine == ESPCoreDumpElfFile.EM_XTENSA:
            if extra_note and extra_info:
                xtensa.print_exc_regs_info(extra_info)
            else:
                print('Exception registers have not been found!')
        print(self.gdb_esp.run_cmd('info registers'))

    def print_isr_context(self, extra_info):
        if self.exe_elf.e_machine == ESPCoreDumpElfFile.EM_XTENSA:
            isr_ctx_idx = XTENSA_ISR_CTX_IDX
        else:
            isr_ctx_idx = RISCV_ISR_CTX_IDX
        if (len(extra_info) < isr_ctx_idx + 1):
            # no information
            return
        if extra_info[isr_ctx_idx]:
            print('Crash happened in the interrupt context')
        else:
            print('Crashed task is not in the interrupt context')

    def print_current_thread_stack(self, task_info):  # type: (Optional[list[Container]]) -> None
        print(self.gdb_esp.run_cmd('bt'))
        if task_info and task_info[0].task_flags != TASK_STATUS_CORRECT:
            print('The current crashed task is corrupted.')
            print('Task #%d info: flags, tcb, stack (%x, %x, %x).' % (task_info[0].task_index,
                                                                      task_info[0].task_flags,
                                                                      task_info[0].task_tcb_addr,
                                                                      task_info[0].task_stack_start))

    def print_all_memory_regions(self):  # type: () -> None
        print('Name   Address   Size   Attrs')
        core_segs = self.core_elf.load_segments
        merged_segs = []
        for sec in self.exe_elf.sections:
            merged = False
            for seg in core_segs:
                if seg.addr <= sec.addr <= seg.addr + len(seg.data):
                    # sec:    |XXXXXXXXXX|
                    # seg: |...XXX.............|
                    seg_addr = seg.addr
                    if seg.addr + len(seg.data) <= sec.addr + len(sec.data):
                        # sec:        |XXXXXXXXXX|
                        # seg:    |XXXXXXXXXXX...|
                        # merged: |XXXXXXXXXXXXXX|
                        seg_len = len(sec.data) + (sec.addr - seg.addr)
                    else:
                        # sec:        |XXXXXXXXXX|
                        # seg:    |XXXXXXXXXXXXXXXXX|
                        # merged: |XXXXXXXXXXXXXXXXX|
                        seg_len = len(seg.data)
                    merged_segs.append((sec.name, seg_addr, seg_len, sec.attr_str(), True))
                    core_segs.remove(seg)
                    merged = True
                elif sec.addr <= seg.addr <= sec.addr + len(sec.data):
                    # sec:  |XXXXXXXXXX|
                    # seg:  |...XXX.............|
                    seg_addr = sec.addr
                    if (seg.addr + len(seg.data)) >= (sec.addr + len(sec.data)):
                        # sec:    |XXXXXXXXXX|
                        # seg:    |..XXXXXXXXXXX|
                        # merged: |XXXXXXXXXXXXX|
                        seg_len = len(sec.data) + (seg.addr + len(seg.data)) - (sec.addr + len(sec.data))
                    else:
                        # sec:    |XXXXXXXXXX|
                        # seg:      |XXXXXX|
                        # merged: |XXXXXXXXXX|
                        seg_len = len(sec.data)
                    merged_segs.append((sec.name, seg_addr, seg_len, sec.attr_str(), True))
                    core_segs.remove(seg)
                    merged = True

            if not merged:
                merged_segs.append((sec.name, sec.addr, len(sec.data), sec.attr_str(), False))

        for ms in merged_segs:
            print('%s 0x%x 0x%x %s' % (ms[0], ms[1], ms[2], ms[3]))

        for cs in core_segs:
            # core dump exec segments are from ROM, other are belong to tasks (TCB or stack)
            if cs.flags & ElfSegment.PF_X:
                seg_name = 'rom.text'
            else:
                seg_name = 'tasks.data'
            print('.coredump.%s 0x%x 0x%x %s' % (seg_name, cs.addr, len(cs.data), cs.attr_str()))

    def print_core_dump_memory_contents(self):  # type: () -> None
        for cs in self.core_elf.load_segments:
            # core dump exec segments are from ROM, other are belong to tasks (TCB or stack)
            if cs.flags & ElfSegment.PF_X:
                seg_name = 'rom.text'
            else:
                seg_name = 'tasks.data'
            print('.coredump.%s 0x%x 0x%x %s' % (seg_name, cs.addr, len(cs.data), cs.attr_str()))
            print(self.gdb_esp.run_cmd('x/%dx 0x%x' % (len(cs.data) // 4, cs.addr)))

    def verify_target(self, core_header_info_dict):
        target = core_header_info_dict.get('target')
        if target is None:
            target = self.get_target()
            core_header_info_dict['target'] = target
        return target

    @contextmanager
    def _handle_coredump_loader_error(self):
        try:
            yield
        except ESPCoreDumpLoaderError as e:
            print(f'Failed to load core dump: {e}', file=sys.stderr)
            if e.extra_output:
                print('', file=sys.stderr)
                print('┌────── Additional information about the error: ', file=sys.stderr)
                print('│   ', file=sys.stderr)
                print(textwrap.indent(e.extra_output, '│   '), file=sys.stderr)
                print('│   ', file=sys.stderr)
                print('└────── end of additional information about the error.', file=sys.stderr)
            raise SystemExit(1)

    def dbg_corefile(self):  # type: () -> Optional[list[str]]
        """
        Command to load core dump from file or flash and run GDB debug session with it
        """
        exe_elf = ESPCoreDumpElfFile(self.prog)
        with self._handle_coredump_loader_error():
            core_header_info_dict = self.get_core_header_info_dict(e_machine=exe_elf.e_machine)
            self.core_elf = ESPCoreDumpElfFile(core_header_info_dict['core_elf_path'])

        temp_files = core_header_info_dict.pop('temp_files')
        self.chip = self.verify_target(core_header_info_dict)

        gdb_args = self.get_gdb_args(is_dbg_mode=True, **core_header_info_dict)

        p = subprocess.Popen(bufsize=0,
                             args=gdb_args,
                             stdin=None, stdout=None, stderr=None,
                             close_fds=CLOSE_FDS)
        p.wait()
        print('Done!')
        return temp_files  # type: ignore

    def info_corefile(self):  # type: () -> Optional[list[str]]
        """
        Command to load core dump from file or flash and print it's data in user friendly form
        """
        with self._handle_coredump_loader_error():
            self.exe_elf = ESPCoreDumpElfFile(self.prog)
            core_header_info_dict = self.get_core_header_info_dict(e_machine=self.exe_elf.e_machine)
            self.core_elf = ESPCoreDumpElfFile(core_header_info_dict['core_elf_path'])

        temp_files = core_header_info_dict.pop('temp_files')
        self.chip = self.verify_target(core_header_info_dict)

        if self.exe_elf.e_machine != self.core_elf.e_machine:
            raise ValueError('The arch should be the same between core elf and exe elf')

        task_info, extra_note = self.get_task_info_extra_note_tuple()

        print('===============================================================')
        print('==================== ESP32 CORE DUMP START ====================')

        gdb_args = self.get_gdb_args(is_dbg_mode=False, **core_header_info_dict)

        self.gdb_esp = EspGDB(gdb_args, timeout_sec=self.gdb_timeout_sec)

        extra_info = None
        if extra_note:
            extra_info = Struct('regs' / GreedyRange(Int32ul)).parse(extra_note.desc).regs
            marker = extra_info[0]
            self.print_crashed_task_info(marker)
            self.print_isr_context(extra_info)

        panic_details = self.get_panic_details()
        if panic_details:
            print('Panic reason: ' + panic_details.desc.decode('utf-8'))

        print('\n================== CURRENT THREAD REGISTERS ===================')
        # Only xtensa have exception registers
        self.print_current_thread_registers(extra_note, extra_info)

        print('\n==================== CURRENT THREAD STACK =====================')
        self.print_current_thread_stack(task_info)
        print('\n======================== THREADS INFO =========================')
        self.print_threads_info(task_info)
        print('\n\n======================= ALL MEMORY REGIONS ========================')
        self.print_all_memory_regions()

        if self.print_mem:
            print('\n====================== CORE DUMP MEMORY CONTENTS ========================')
            self.print_core_dump_memory_contents()

        print('\n===================== ESP32 CORE DUMP END =====================')
        print('===============================================================')

        del self.gdb_esp
        print('Done!')
        return temp_files  # type: ignore
