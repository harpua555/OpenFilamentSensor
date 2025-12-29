#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#
# This script will parse soc.h and generate .py file containing all of the target constants.
# Once one manually adds a new target, the constants file can be regenerated.
# The script is ESP-IDF dependent and is not intended for use by end users.
#
import os
import sys
from ast import literal_eval

from esp_coredump.coredump import IDF_SETUP_ERROR
from esp_coredump.corefile import SUPPORTED_TARGETS

IDF_PATH = os.getenv('IDF_PATH')


def main():  # type: () -> None
    constants = [
        'SOC_DRAM_LOW',
        'SOC_DRAM_HIGH',
        'SOC_IRAM_LOW',
        'SOC_IRAM_HIGH',
        'SOC_RTC_DATA_LOW',
        'SOC_RTC_DATA_HIGH',
        'SOC_RTC_DRAM_LOW',
        'SOC_RTC_DRAM_HIGH',
    ]

    for target in SUPPORTED_TARGETS:
        target_constants = {}
        soc_header_fp = os.path.join(IDF_PATH, 'components/soc/{}/include/soc/soc.h'.format(target))  # type: ignore
        module_fp = os.path.join(os.path.normpath(os.path.abspath(os.path.dirname(__file__))),
                                 'soc_headers',
                                 '{}.py'.format(target)
                                 )

        with open(soc_header_fp) as fr:
            for line in fr.readlines():
                for attr in constants:
                    if '#define {}'.format(attr) in line:
                        target_constants[attr] = literal_eval(line.strip().split()[-1])

        for attr in constants:
            if attr not in target_constants:
                print(f'WARNING: {attr} is missing in {soc_header_fp}, check if this is correct!', file=sys.stderr)
                target_constants[attr] = 2**32 - 1

        with open(module_fp, 'w') as fw:
            for k, v in target_constants.items():
                fw.write('{} = {}\n'.format(k, hex(v).lower()))


if __name__ == '__main__':
    if not IDF_PATH:
        print(IDF_SETUP_ERROR)
        sys.exit(1)
    main()
