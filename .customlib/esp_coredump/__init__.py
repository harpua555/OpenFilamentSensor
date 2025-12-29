#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#

import os
import sys

try:
    import esptool  # noqa: F401
except ImportError:
    idf_path = os.getenv('IDF_PATH')
    if not idf_path or not os.path.exists(idf_path):
        raise ModuleNotFoundError('Please read the README file for the latest installation instructions')
    sys.path.insert(0, os.path.join(idf_path, 'components', 'esptool_py', 'esptool'))
    import esptool  # noqa: F401

from .coredump import CoreDump

__all__ = [
    'CoreDump',
]

__version__ = '1.14.0'
