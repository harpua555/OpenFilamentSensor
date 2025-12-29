#
# SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import os.path

from esp_coredump import CoreDump, __version__
from esp_coredump.cli_ext import parser


def main():
    print(f'espcoredump.py v{__version__}', flush=True)

    args = parser.parse_args()
    debug = getattr(args, 'debug', 3)
    if debug == 0:
        log_level = logging.CRITICAL
    elif debug == 1:
        log_level = logging.ERROR
    elif debug == 2:
        log_level = logging.WARNING
    elif debug == 3:
        log_level = logging.INFO
    else:
        log_level = logging.DEBUG
    logging.basicConfig(format='%(levelname)s: %(message)s', level=log_level)

    kwargs = {k: v for k, v in vars(args).items() if v is not None}

    kwargs.pop('debug', None)
    kwargs.pop('operation', None)

    espcoredump = CoreDump(**kwargs)
    temp_core_files = None

    try:
        if args.operation == 'info_corefile':
            temp_core_files = espcoredump.info_corefile()
        elif args.operation == 'dbg_corefile':
            temp_core_files = espcoredump.dbg_corefile()
    finally:
        if temp_core_files:
            for f in temp_core_files:
                try:
                    os.remove(f)
                except OSError:
                    pass


if __name__ == '__main__':
    main()
