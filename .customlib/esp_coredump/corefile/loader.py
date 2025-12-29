#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#

import base64
import binascii
import hashlib
import logging
import os
import subprocess
import sys
import tempfile
from base64 import b64decode
from typing import Optional, Tuple

from construct import (AlignedStruct, Bytes, GreedyRange, Int32ul, Padding,
                       Struct, abs_, this)

from . import ESPCoreDumpLoaderError
from .elf import (TASK_STATUS_CORRECT, TASK_STATUS_TCB_CORRUPTED, ElfFile,
                  ElfSegment, ESPCoreDumpElfFile, EspTaskStatus, NoteSection)
from .riscv import (Esp32C2Methods, Esp32C3Methods, Esp32C5Methods,
                    Esp32C6Methods, Esp32C61Methods, Esp32H2Methods,
                    Esp32H4Methods, Esp32H21Methods, Esp32P4Methods)
from .xtensa import Esp32Methods, Esp32S2Methods, Esp32S3Methods

IDF_PATH = os.getenv('IDF_PATH', '')
PARTTOOL_PY = os.path.join(IDF_PATH, 'components', 'partition_table', 'parttool.py')

# Following structs are based on source code
# components/espcoredump/include_core_dump/esp_core_dump_priv.h

EspCoreDumpV1Header = Struct(
    'tot_len' / Int32ul,
    'ver' / Int32ul,
    'task_num' / Int32ul,
    'tcbsz' / Int32ul,
)

EspCoreDumpV2Header = Struct(
    'tot_len' / Int32ul,
    'ver' / Int32ul,
    'task_num' / Int32ul,
    'tcbsz' / Int32ul,
    'segs_num' / Int32ul,
)

EspCoreDumpV2_1_Header = Struct(
    'tot_len' / Int32ul,
    'ver' / Int32ul,
    'task_num' / Int32ul,
    'tcbsz' / Int32ul,
    'segs_num' / Int32ul,
    'chip_rev' / Int32ul,
)

EspCoreDumpV2_2_Header = Struct(
    'tot_len' / Int32ul,
    'ver' / Int32ul,
    'chip_rev' / Int32ul,
)

CRC = Int32ul
SHA256 = Bytes(32)

TaskHeader = Struct(
    'tcb_addr' / Int32ul,
    'stack_top' / Int32ul,
    'stack_end' / Int32ul,
)

MemSegmentHeader = Struct(
    'mem_start' / Int32ul,
    'mem_sz' / Int32ul,
    'data' / Bytes(this.mem_sz),
)


def get_core_file_format(core_file: str) -> str:
    """Get format of core_file based on the header"""
    with open(core_file, 'rb') as f:
        coredump_bytes = f.read(16)

        # Check if this is an ELF file without the core dump header (core_dump_header_t)
        if coredump_bytes.startswith(b'\x7fELF'):
            return 'elf'

        # Check if this is a core dump with a core_dump_header_t header
        core_version = EspCoreDumpVersion(int.from_bytes(coredump_bytes[4:7], 'little'))
        if core_version.dump_ver in EspCoreDumpLoader.CORE_VERSIONS:
            return 'raw'

    # Neither of theses headers matched, so this might be a base64 encoded core dump;
    # however in case it's just some unknown binary, ignore decoding errors.
    with open(core_file, 'r', encoding='utf-8', errors='ignore') as c:
        coredump_str = c.read()
        try:
            b64decode(coredump_str)
        except Exception:
            raise SystemExit(
                'The format of the provided core-file is not recognized. '
                'Please ensure that the core-format matches one of the following: ELF (“elf”), '
                'raw (raw) or base64-encoded (b64) binary'
            )
        return 'b64'


class EspCoreDumpVersion(object):
    """Core dump version class, it contains all version-dependent params
    """
    # Chip IDs should be in sync with components/esp_hw_support/include/esp_chip_info.h
    ESP32 = 0
    ESP32S2 = 2
    ESP32S3 = 9
    XTENSA_CHIPS = [ESP32, ESP32S2, ESP32S3]

    ESP32C3 = 5
    ESP32C2 = 12
    ESP32C6 = 13
    ESP32H2 = 16
    ESP32P4 = 18
    ESP32C61 = 20
    ESP32C5 = 23
    ESP32H21 = 25
    ESP32H4 = 28
    RISCV_CHIPS = [ESP32C3, ESP32C2, ESP32H2, ESP32C6, ESP32P4, ESP32C5, ESP32C61, ESP32H21, ESP32H4]

    COREDUMP_SUPPORTED_TARGETS = XTENSA_CHIPS + RISCV_CHIPS

    def __init__(self, version=None):  # type: (int) -> None
        """Constructor for core dump version
        """
        super(EspCoreDumpVersion, self).__init__()
        if version is None:
            self.version = 0
        else:
            self.set_version(version)

    @staticmethod
    def make_dump_ver(major, minor):  # type: (int, int) -> int
        return ((major & 0xFF) << 8) | ((minor & 0xFF) << 0)

    def set_version(self, version):  # type: (int) -> None
        self.version = version

    @property
    def chip_ver(self):  # type: () -> int
        return (self.version & 0xFFFF0000) >> 16

    @property
    def dump_ver(self):  # type: () -> int
        return self.version & 0x0000FFFF

    @property
    def major(self):  # type: () -> int
        return (self.version & 0x0000FF00) >> 8

    @property
    def minor(self):  # type: () -> int
        return self.version & 0x000000FF


class EspCoreDumpLoader(EspCoreDumpVersion):
    # "legacy" stands for core dumps v0.1 (before IDF v4.1)
    BIN_V1 = EspCoreDumpVersion.make_dump_ver(0, 1)
    BIN_V2 = EspCoreDumpVersion.make_dump_ver(0, 2)
    BIN_V2_1 = EspCoreDumpVersion.make_dump_ver(0, 3)
    ELF_CRC32_V2 = EspCoreDumpVersion.make_dump_ver(1, 0)
    ELF_CRC32_V2_1 = EspCoreDumpVersion.make_dump_ver(1, 2)
    ELF_SHA256_V2 = EspCoreDumpVersion.make_dump_ver(1, 1)
    ELF_SHA256_V2_1 = EspCoreDumpVersion.make_dump_ver(1, 3)
    ELF_SHA256_V2_2 = EspCoreDumpVersion.make_dump_ver(1, 4)
    CORE_VERSIONS = [BIN_V1, BIN_V2, BIN_V2_1, ELF_CRC32_V2, ELF_CRC32_V2_1, ELF_SHA256_V2, ELF_SHA256_V2_1, ELF_SHA256_V2_2]

    def __init__(self):  # type: () -> None
        super(EspCoreDumpLoader, self).__init__()
        self.core_src_file = None  # type: Optional[str]
        self.core_src_struct = None
        self.core_src = None

        self.core_elf_file: str = None  # type: ignore

        self.header = None
        self.header_struct = EspCoreDumpV1Header
        self.checksum_struct = CRC

        # target classes will be assigned in ``_reload_coredump``
        self.target_methods = Esp32Methods()

        self.temp_files = []  # type: list[str]

    def _create_temp_file(self):  # type: () -> str
        t = tempfile.NamedTemporaryFile('wb', delete=False)
        # Here we close this at first to make sure the read/write is wrapped in context manager
        # Otherwise the result will be wrong if you read while open in another session
        t.close()
        self.temp_files.append(t.name)
        return t.name

    def _load_core_src(self):  # type: () -> str
        """
        Write core elf into ``self.core_src``,
        Return the target str by reading core elf
        """
        with open(self.core_src_file, 'rb') as fr:  # type: ignore
            coredump_bytes = fr.read()

        _header = EspCoreDumpV1Header.parse(coredump_bytes)  # first we use V1 format to get version
        self.set_version(_header.ver)
        if self.dump_ver == self.ELF_CRC32_V2:
            self.checksum_struct = CRC
            self.header_struct = EspCoreDumpV2Header
        elif self.dump_ver == self.ELF_CRC32_V2_1:
            self.checksum_struct = CRC
            self.header_struct = EspCoreDumpV2_1_Header
        elif self.dump_ver == self.ELF_SHA256_V2:
            self.checksum_struct = SHA256
            self.header_struct = EspCoreDumpV2Header
        elif self.dump_ver == self.ELF_SHA256_V2_1:
            self.checksum_struct = SHA256
            self.header_struct = EspCoreDumpV2_1_Header
        elif self.dump_ver == self.ELF_SHA256_V2_2:
            self.checksum_struct = SHA256
            self.header_struct = EspCoreDumpV2_2_Header
        elif self.dump_ver == self.BIN_V1:
            self.checksum_struct = CRC
            self.header_struct = EspCoreDumpV1Header
        elif self.dump_ver == self.BIN_V2:
            self.checksum_struct = CRC
            self.header_struct = EspCoreDumpV2Header
        elif self.dump_ver == self.BIN_V2_1:
            self.checksum_struct = CRC
            self.header_struct = EspCoreDumpV2_1_Header
        else:
            raise ESPCoreDumpLoaderError('Core dump version "0x%x" is not supported!' % self.dump_ver)

        self.header = self.header_struct.parse(coredump_bytes)

        self.core_src_struct = Struct(
            'header' / self.header_struct,
            'data' / Bytes(this.header.tot_len - self.header_struct.sizeof() - self.checksum_struct.sizeof()),
            'checksum' / self.checksum_struct,
        )
        self.core_src = self.core_src_struct.parse(coredump_bytes)  # type: ignore

        if self.header and self.header.get('chip_rev') is not None:
            self.chip_rev = self.header.chip_rev  # type: ignore
        else:
            self.chip_rev = None  # type: ignore

        if self.chip_ver in self.COREDUMP_SUPPORTED_TARGETS:
            if self.chip_ver == self.ESP32:
                self.target_methods = Esp32Methods()  # type: ignore
            elif self.chip_ver == self.ESP32S2:
                self.target_methods = Esp32S2Methods()  # type: ignore
            elif self.chip_ver == self.ESP32C3:
                self.target_methods = Esp32C3Methods()  # type: ignore
            elif self.chip_ver == self.ESP32S3:
                self.target_methods = Esp32S3Methods()  # type: ignore
            elif self.chip_ver == self.ESP32C2:
                self.target_methods = Esp32C2Methods()  # type: ignore
            elif self.chip_ver == self.ESP32H2:
                self.target_methods = Esp32H2Methods()  # type: ignore
            elif self.chip_ver == self.ESP32C6:
                self.target_methods = Esp32C6Methods()  # type: ignore
            elif self.chip_ver == self.ESP32P4:
                self.target_methods = Esp32P4Methods()  # type: ignore
            elif self.chip_ver == self.ESP32C5:
                self.target_methods = Esp32C5Methods()  # type: ignore
            elif self.chip_ver == self.ESP32C61:
                self.target_methods = Esp32C61Methods()  # type: ignore
            elif self.chip_ver == self.ESP32H21:
                self.target_methods = Esp32H21Methods()  # type: ignore
            elif self.chip_ver == self.ESP32H4:
                self.target_methods = Esp32H4Methods()  # type: ignore
            else:
                raise NotImplementedError
        else:
            raise ESPCoreDumpLoaderError('Core dump chip "0x%x" is not supported!' % self.chip_ver)

        return self.target_methods.TARGET  # type: ignore

    def _validate_dump_file(self):  # type: () -> None
        if self.chip_ver not in self.COREDUMP_SUPPORTED_TARGETS:
            raise ESPCoreDumpLoaderError('Invalid core dump chip version: "{}", should be <= "0x{:X}"'
                                         .format(self.chip_ver, self.ESP32S2))
        if self.checksum_struct == CRC:
            self._crc_validate()
        elif self.checksum_struct == SHA256:
            self._sha256_validate()

    def _crc_validate(self):  # type: () -> None
        if self.dump_ver in [self.BIN_V2_1,
                             self.ELF_CRC32_V2_1]:
            data_crc = binascii.crc32(
                EspCoreDumpV2_1_Header.build(self.core_src.header) + self.core_src.data) & 0xffffffff  # type: ignore
        else:
            data_crc = binascii.crc32(
                EspCoreDumpV2Header.build(self.core_src.header) + self.core_src.data) & 0xffffffff  # type: ignore
        if data_crc != self.core_src.checksum:  # type: ignore
            raise ESPCoreDumpLoaderError(
                'Invalid core dump CRC %x, should be %x' % (data_crc, self.core_src.checksum))  # type: ignore

    def _sha256_validate(self):  # type: () -> None
        if self.dump_ver in [self.ELF_SHA256_V2_1]:
            data_sha256 = hashlib.sha256(
                EspCoreDumpV2_1_Header.build(self.core_src.header) + self.core_src.data)  # type: ignore
        elif self.dump_ver in [self.ELF_SHA256_V2_2]:
            data_sha256 = hashlib.sha256(
                EspCoreDumpV2_2_Header.build(self.core_src.header) + self.core_src.data)  # type: ignore
        else:
            data_sha256 = hashlib.sha256(
                EspCoreDumpV2Header.build(self.core_src.header) + self.core_src.data)  # type: ignore

        data_sha256_str = data_sha256.hexdigest()
        sha256_str = binascii.hexlify(self.core_src.checksum).decode('ascii')  # type: ignore
        if data_sha256_str != sha256_str:
            raise ESPCoreDumpLoaderError('Invalid core dump SHA256 "{}", should be "{}"'
                                         .format(data_sha256_str, sha256_str))

    def create_corefile(self, exe_name=None, e_machine=ESPCoreDumpElfFile.EM_XTENSA):
        # type: (Optional[str], Optional[int]) -> None
        """
        Creates core dump ELF file
        """
        self._validate_dump_file()
        self.core_elf_file = self._create_temp_file()

        if self.dump_ver in [self.ELF_CRC32_V2,
                             self.ELF_CRC32_V2_1,
                             self.ELF_SHA256_V2,
                             self.ELF_SHA256_V2_1,
                             self.ELF_SHA256_V2_2]:
            self._extract_elf_corefile(exe_name, e_machine)
        elif self.dump_ver in [self.BIN_V1,
                               self.BIN_V2,
                               self.BIN_V2_1]:
            self._extract_bin_corefile(e_machine)
        else:
            raise NotImplementedError

    def _extract_elf_corefile(self, exe_name=None, e_machine=ESPCoreDumpElfFile.EM_XTENSA):
        # type: (str, Optional[int]) -> None
        """
        Reads the ELF formatted core dump image and parse it
        """
        with open(self.core_elf_file, 'wb') as fw:  # type: ignore
            fw.write(self.core_src.data)  # type: ignore

        core_elf = ESPCoreDumpElfFile(self.core_elf_file, e_machine=e_machine)  # type: ignore

        if self.chip_rev is not None:  # type: ignore
            chip_rev_note = b''
            chip_rev_note += self._build_note_section('ESP_CHIP_REV',
                                                      ESPCoreDumpElfFile.PT_ESP_INFO,
                                                      Int32ul.build(self.chip_rev))  # type: ignore
            try:
                core_elf.add_segment(0, chip_rev_note, ElfFile.PT_NOTE, 0)
            except ESPCoreDumpLoaderError as e:
                logging.warning('Skip core dump info NOTES segment {:d} bytes @ 0x{:x}. (Reason: {})'
                                .format(len(chip_rev_note), 0, e))
            core_elf.dump(self.core_elf_file)

        # Read note segments from core file which are belong to tasks (TCB or stack)
        for seg in core_elf.note_segments:
            for note_sec in seg.note_secs:
                # Check for version info note
                if note_sec.name == b'ESP_CORE_DUMP_INFO' \
                        and note_sec.type == ESPCoreDumpElfFile.PT_ESP_INFO \
                        and exe_name:
                    exe_elf = ElfFile(exe_name)
                    app_sha256 = binascii.hexlify(exe_elf.sha256)
                    coredump_sha256_struct = Struct(
                        'ver' / Int32ul,
                        'sha256' / Bytes(64)  # SHA256 as hex string
                    )
                    coredump_sha256 = coredump_sha256_struct.parse(note_sec.desc[:coredump_sha256_struct.sizeof()])

                    logging.debug('App SHA256: {!r}'.format(app_sha256))
                    logging.debug('Core dump SHA256: {!r}'.format(coredump_sha256))

                    # Actual coredump SHA may be shorter than a full SHA256 hash
                    # with NUL byte padding, according to the app's APP_RETRIEVE_LEN_ELF_SHA
                    # length
                    core_sha_trimmed = coredump_sha256.sha256.rstrip(b'\x00').decode()
                    app_sha_trimmed = app_sha256[:len(core_sha_trimmed)].decode()

                    if core_sha_trimmed != app_sha_trimmed:
                        print("WARNING: SHA256 mismatch! (Coredump: {}, App: {}). Proceeding anyway...".format(core_sha_trimmed, app_sha_trimmed))
                        #raise ESPCoreDumpLoaderError(
                        #    'Invalid application image for coredump: coredump SHA256({}) != app SHA256({}).'
                        #    .format(core_sha_trimmed, app_sha_trimmed))
                    if coredump_sha256.ver != self.version:
                        print("WARNING: SHA256 version mismatch! Proceeding anyway...")
                        #raise ESPCoreDumpLoaderError(
                        #    'Invalid application image for coredump: coredump SHA256 version({}) != app SHA256 version({}).'
                        #    .format(coredump_sha256.ver, self.version))

    @staticmethod
    def _get_aligned_size(size, align_with=4):  # type: (int, int) -> int
        if size % align_with:
            return align_with * (size // align_with + 1)
        return size

    @staticmethod
    def _build_note_section(name, sec_type, desc):  # type: (str, int, str) -> bytes
        b_name = bytearray(name, encoding='ascii') + b'\0'
        return NoteSection.build({  # type: ignore
            'namesz': len(b_name),
            'descsz': len(desc),
            'type': sec_type,
            'name': b_name,
            'desc': desc,
        })

    def _extract_bin_corefile(self, e_machine=ESPCoreDumpElfFile.EM_XTENSA):  # type: (Optional[int]) -> None
        """
        Creates core dump ELF file
        """
        coredump_data_struct = Struct(
            'tasks' / GreedyRange(
                AlignedStruct(
                    4,
                    'task_header' / TaskHeader,
                    'tcb' / Bytes(self.header.tcbsz),  # type: ignore
                    'stack' / Bytes(abs_(this.task_header.stack_top - this.task_header.stack_end)),  # type: ignore
                )
            ),
            'mem_seg_headers' / MemSegmentHeader[self.core_src.header.segs_num]  # type: ignore
        )
        core_elf = ESPCoreDumpElfFile(e_machine=e_machine)
        notes = b''
        core_dump_info_notes = b''
        task_info_notes = b''

        coredump_data = coredump_data_struct.parse(self.core_src.data)  # type: ignore
        for i, task in enumerate(coredump_data.tasks):
            stack_len_aligned = self._get_aligned_size(abs(task.task_header.stack_top - task.task_header.stack_end))
            task_status_kwargs = {
                'task_index': i,
                'task_flags': TASK_STATUS_CORRECT,
                'task_tcb_addr': task.task_header.tcb_addr,
                'task_stack_start': min(task.task_header.stack_top, task.task_header.stack_end),
                'task_stack_end': max(task.task_header.stack_top, task.task_header.stack_end),
                'task_stack_len': stack_len_aligned,
                'task_name': Padding(16).build({})  # currently we don't have task_name, keep it as padding
            }

            # Write TCB
            try:
                if self.target_methods.tcb_is_sane(task.task_header.tcb_addr, self.header.tcbsz):  # type: ignore
                    core_elf.add_segment(task.task_header.tcb_addr,
                                         task.tcb,
                                         ElfFile.PT_LOAD,
                                         ElfSegment.PF_R | ElfSegment.PF_W)
                elif task.task_header.tcb_addr and self.target_methods.addr_is_fake(task.task_header.tcb_addr):
                    task_status_kwargs['task_flags'] |= TASK_STATUS_TCB_CORRUPTED
            except ESPCoreDumpLoaderError as e:
                logging.warning('Skip TCB {} bytes @ 0x{:x}. (Reason: {})'
                                .format(self.header.tcbsz, task.task_header.tcb_addr, e))  # type: ignore

            # Write stack
            try:
                if self.target_methods.stack_is_sane(task_status_kwargs['task_stack_start'],
                                                     task_status_kwargs['task_stack_end']):
                    core_elf.add_segment(task_status_kwargs['task_stack_start'],
                                         task.stack,
                                         ElfFile.PT_LOAD,
                                         ElfSegment.PF_R | ElfSegment.PF_W)
                elif (task_status_kwargs['task_stack_start']
                      and self.target_methods.addr_is_fake(task_status_kwargs['task_stack_start'])):
                    task_status_kwargs['task_flags'] |= TASK_STATUS_TCB_CORRUPTED
                    core_elf.add_segment(task_status_kwargs['task_stack_start'],
                                         task.stack,
                                         ElfFile.PT_LOAD,
                                         ElfSegment.PF_R | ElfSegment.PF_W)
            except ESPCoreDumpLoaderError as e:
                logging.warning('Skip task\'s ({:x}) stack {} bytes @ 0x{:x}. (Reason: {})'
                                .format(task_status_kwargs['tcb_addr'],
                                        task_status_kwargs['stack_len_aligned'],
                                        task_status_kwargs['stack_base'],
                                        e))

            try:
                logging.debug('Stack start_end: 0x{:x} @ 0x{:x}'
                              .format(task.task_header.stack_top, task.task_header.stack_end))
                task_regs, extra_regs = self.target_methods.get_registers_from_stack(
                    task.stack,
                    task.task_header.stack_end > task.task_header.stack_top
                )
            except Exception as e:
                raise ESPCoreDumpLoaderError(str(e))

            task_info_notes += self._build_note_section('TASK_INFO',
                                                        ESPCoreDumpElfFile.PT_ESP_TASK_INFO,
                                                        EspTaskStatus.build(task_status_kwargs))
            notes += self._build_note_section('CORE',
                                              ElfFile.PT_LOAD,
                                              self.target_methods.build_prstatus_data(task.task_header.tcb_addr,
                                                                                      task_regs))

            if len(core_dump_info_notes) == 0:  # the first task is the crashed task
                core_dump_info_notes += self._build_note_section('ESP_CORE_DUMP_INFO',
                                                                 ESPCoreDumpElfFile.PT_ESP_INFO,
                                                                 Int32ul.build(self.header.ver))  # type: ignore
                _regs = [task.task_header.tcb_addr]

                # For xtensa, we need to put the exception registers into the extra info as well
                if e_machine == ESPCoreDumpElfFile.EM_XTENSA and extra_regs:
                    for reg_id in extra_regs:
                        _regs.extend([reg_id, extra_regs[reg_id]])

                core_dump_info_notes += self._build_note_section(
                    'EXTRA_INFO',
                    ESPCoreDumpElfFile.PT_ESP_EXTRA_INFO,
                    Int32ul[len(_regs)].build(_regs)
                )

        if self.dump_ver == self.BIN_V2:
            for header in coredump_data.mem_seg_headers:
                logging.debug('Read memory segment {} bytes @ 0x{:x}'.format(header.mem_sz, header.mem_start))
                core_elf.add_segment(header.mem_start, header.data, ElfFile.PT_LOAD, ElfSegment.PF_R | ElfSegment.PF_W)

        # add notes
        try:
            core_elf.add_segment(0, notes, ElfFile.PT_NOTE, 0)
        except ESPCoreDumpLoaderError as e:
            logging.warning('Skip NOTES segment {:d} bytes @ 0x{:x}. (Reason: {})'.format(len(notes), 0, e))
        # add core dump info notes
        try:
            core_elf.add_segment(0, core_dump_info_notes, ElfFile.PT_NOTE, 0)
        except ESPCoreDumpLoaderError as e:
            logging.warning('Skip core dump info NOTES segment {:d} bytes @ 0x{:x}. (Reason: {})'
                            .format(len(core_dump_info_notes), 0, e))
        try:
            core_elf.add_segment(0, task_info_notes, ElfFile.PT_NOTE, 0)
        except ESPCoreDumpLoaderError as e:
            logging.warning('Skip failed tasks info NOTES segment {:d} bytes @ 0x{:x}. (Reason: {})'
                            .format(len(task_info_notes), 0, e))
        # dump core ELF
        core_elf.e_type = ElfFile.ET_CORE
        core_elf.dump(self.core_elf_file)  # type: ignore


class ESPCoreDumpFlashLoader(EspCoreDumpLoader):
    ESP_COREDUMP_PART_TABLE_OFF = 0x8000

    def __init__(self, offset, target=None, port=None, baud=None, part_table_offset=0x8000):
        # type: (Optional[int], Optional[str], Optional[str], Optional[int], Optional[int]) -> None
        # TODO in next major release drop offset argument and use just parttool to find offset of coredump partition
        super(ESPCoreDumpFlashLoader, self).__init__()
        self.port = port
        self.baud = baud
        self.part_table_offset = part_table_offset

        self._get_core_src(offset, target)
        self.target = self._load_core_src()

    def _get_core_src(self, off, target=None):  # type: (Optional[int], Optional[str]) -> None
        """
        Loads core dump from flash using esptool
        """
        logging.info('Invoke esptool to read image.')
        self._invoke_esptool(off=off, target=target)

    def _invoke_esptool(self, off=None, target=None):  # type: (Optional[int], Optional[str]) -> None
        """
        Loads core dump from flash using elftool
        """
        if target is None:
            target = 'auto'
        tool_args = [sys.executable, '-m', 'esptool', '-c', target]
        if self.port:
            tool_args.extend(['-p', self.port])
        if self.baud:
            tool_args.extend(['-b', str(self.baud)])

        self.core_src_file = self._create_temp_file()
        try:
            (part_offset, part_size) = self._get_core_dump_partition_info()
            if not off:
                off = part_offset  # set default offset if not specified
                logging.warning('The core dump image offset is not specified. Use partition offset: 0x%x.', part_offset)
            if part_offset != off:
                logging.warning('Predefined image offset: %d does not match core dump partition offset: 0x%x', off,
                                part_offset)

            # Here we use V1 format to locate the size
            tool_args.extend(['read_flash', str(off), str(EspCoreDumpV1Header.sizeof())])
            tool_args.append(self.core_src_file)  # type: ignore

            # read core dump length
            et_out = subprocess.check_output(tool_args, stderr=subprocess.STDOUT)
            if et_out:
                logging.info(et_out.decode('utf-8'))

            header = EspCoreDumpV1Header.parse(open(self.core_src_file, 'rb').read())  # type: ignore
            if not header or not 0 < header.tot_len <= part_size:
                logging.error('Incorrect size of core dump image: {}, use partition size instead: {}'
                              .format(header.tot_len, part_size))
                coredump_len = part_size
            else:
                coredump_len = header.tot_len
            # set actual size of core dump image and read it from flash
            tool_args[-2] = str(coredump_len)
            et_out = subprocess.check_output(tool_args, stderr=subprocess.STDOUT)
            if et_out:
                logging.info(et_out.decode('utf-8'))
        except subprocess.CalledProcessError as e:
            raise ESPCoreDumpLoaderError(f'esptool script execution failed with error {e.returncode}, '
                                         f"failed command was: '{e.cmd}'",
                                         extra_output=e.output.decode('utf-8', 'ignore'))

    def _get_core_dump_partition_info(self):  # type: () -> Tuple[int, int]
        """
        Get core dump partition info using parttool
        """
        logging.info('Retrieving core dump partition offset and size...')
        part_off = self.part_table_offset or self.ESP_COREDUMP_PART_TABLE_OFF
        try:
            tool_args = [sys.executable, PARTTOOL_PY, '-q', '--partition-table-offset', str(part_off)]
            if self.port:
                tool_args.extend(['--port', self.port])
            invoke_args = tool_args + ['get_partition_info', '--partition-type', 'data',
                                       '--partition-subtype', 'coredump',
                                       '--info', 'offset', 'size']
            res = subprocess.check_output(invoke_args).strip()
            (offset_str, size_str) = res.rsplit(b'\n')[-1].split(b' ')
            size = int(size_str, 16)
            offset = int(offset_str, 16)
            logging.info('Core dump partition offset=%d, size=%d', offset, size)
        except subprocess.CalledProcessError as e:
            raise ESPCoreDumpLoaderError(f'parttool script execution failed with error {e.returncode}, '
                                         f"failed command was: '{' '.join(e.cmd)}'",
                                         extra_output=e.output.decode('utf-8', 'ignore'))
        return offset, size


class ESPCoreDumpFileLoader(EspCoreDumpLoader):
    def __init__(self, path, is_b64=False):  # type: (str, bool) -> None
        super(ESPCoreDumpFileLoader, self).__init__()
        self.is_b64 = is_b64

        self._get_core_src(path)
        self.target = self._load_core_src()

    def _get_core_src(self, path):  # type: (str) -> None
        """
        Loads core dump from (raw binary or base64-encoded) file
        """
        logging.debug('Load core dump from "%s", %s format', path, 'b64' if self.is_b64 else 'raw')
        if not self.is_b64:
            self.core_src_file = path
        else:
            self.core_src_file = self._create_temp_file()
            with open(self.core_src_file, 'wb') as fw:
                with open(path, 'rb') as fb64:
                    while True:
                        line = fb64.readline()
                        if len(line) == 0:
                            break
                        data = base64.standard_b64decode(line.rstrip(b'\r\n'))
                        fw.write(data)  # type: ignore
