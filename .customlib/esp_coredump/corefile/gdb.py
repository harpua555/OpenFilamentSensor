#
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import os
import re
import time
from subprocess import TimeoutExpired

from pygdbmi.gdbcontroller import GdbController

from . import ESPCoreDumpError

DEFAULT_GDB_TIMEOUT_SEC = 3


class EspGDB(object):
    def __init__(self, gdb_args, timeout_sec=DEFAULT_GDB_TIMEOUT_SEC):

        """
        Start GDB and initialize a GdbController instance
        """
        try:
            self.p = GdbController(command=gdb_args)
        except TypeError:
            # fallback for pygdbmi<0.10.0.0.
            self.p = GdbController(gdb_path=gdb_args[0], gdb_args=gdb_args[1:])

        self.timeout = timeout_sec

        # Consume initial output by issuing a dummy command
        self._gdbmi_run_cmd_get_responses(cmd='-data-list-register-values x pc',
                                          resp_message=None, resp_type='console', multiple=True,
                                          done_message='done', done_type='result')

    def __del__(self):
        """EspGDB destructor taking GdbController.gdb_process.exit() and adjusting it to work properly"""
        try:
            if self.p.gdb_process:
                self.p.gdb_process.terminate()
                if os.name != 'nt':
                    # this is causing an Exception on Windows, but is required on UNIX systems
                    self.p.gdb_process.communicate()  # Close pipes (STDIN, STDOUT, STDERR)
                try:
                    self.p.gdb_process.wait(timeout=1)
                except TimeoutExpired:
                    if self.p.gdb_process.returncode is None:
                        self.p.gdb_process.kill()
                self.p.gdb_process = None
        except IndexError:
            logging.warning('Attempt to terminate the GDB process failed, because it is already terminated. Skip')

    def _gdbmi_run_cmd_get_responses(self, cmd, resp_message, resp_type, multiple=True,
                                     done_message=None, done_type=None, response_delay_sec=None):

        self.p.write(cmd, read_response=False)
        t_end = time.time() + (response_delay_sec or self.timeout)
        filtered_response_list = []
        all_responses = []
        while time.time() < t_end:
            more_responses = self.p.get_gdb_response(timeout_sec=0, raise_error_on_timeout=False)
            filtered_response_list += filter(lambda rsp: rsp['message'] == resp_message and rsp['type'] == resp_type,
                                             more_responses)
            all_responses += more_responses
            if filtered_response_list and not multiple:
                break
            if done_message and done_type and self._gdbmi_filter_responses(more_responses, done_message, done_type):
                break
        if not filtered_response_list and not multiple:
            raise ESPCoreDumpError("Couldn't find response with message '{}', type '{}' in responses '{}'".format(
                resp_message, resp_type, str(all_responses)
            ))
        return filtered_response_list

    def _gdbmi_run_cmd_get_one_response(self, cmd, resp_message, resp_type, response_delay_sec=None):

        return self._gdbmi_run_cmd_get_responses(cmd, resp_message, resp_type, response_delay_sec=response_delay_sec,
                                                 multiple=False)[0]

    def _gdbmi_data_evaluate_expression(self, expr):
        """ Get the value of an expression, similar to the 'print' command """
        return self._gdbmi_run_cmd_get_one_response("-data-evaluate-expression \"%s\"" % expr,
                                                    'done', 'result')['payload']['value']

    def get_tcb_variable(self, tcb_addr, variable):
        """ Get FreeRTOS variable from given TCB address """
        try:
            val = self._gdbmi_data_evaluate_expression('(char*)((TCB_t *)0x%x)->%s' % (tcb_addr, variable))
        except (ESPCoreDumpError, KeyError):
            # KeyError is raised when "value" is not in "payload"
            return ''
        return val

    def parse_tcb_variable(self, tcb_addr, variable):
        """ Get FreeRTOS variable from given TCB address """
        val = self.get_tcb_variable(tcb_addr, variable)

        # Value is of form '0x12345678 ""'
        result = re.search(r'0x[0-9a-fA-F]+', val)
        if result:
            return result.group(0)
        return ''

    def get_freertos_task_name(self, tcb_addr):
        """ Get FreeRTOS task name given the TCB address """
        val = self.get_tcb_variable(tcb_addr, 'pcTaskName')

        # Value is of form '0x12345678 "task_name"', extract the actual name
        result = re.search(r"\"([^']*)\"$", val)
        if result:
            return result.group(1)
        return ''

    def run_cmd(self, gdb_cmd):
        """ Execute a generic GDB console command via MI2
        """
        filtered_responses = self._gdbmi_run_cmd_get_responses(cmd="-interpreter-exec console \"%s\"" % gdb_cmd,
                                                               resp_message=None, resp_type='console', multiple=True,
                                                               done_message='done', done_type='result')
        return ''.join([x['payload'] for x in filtered_responses]) \
            .replace('\\n', '\n') \
            .replace('\\t', '\t') \
            .rstrip('\n') \
            .replace('\\"', '"')

    def get_thread_info(self, response_delay_sec=DEFAULT_GDB_TIMEOUT_SEC):
        """ Get information about all threads known to GDB, and the current thread ID """
        result = self._gdbmi_run_cmd_get_one_response('-thread-info', 'done', 'result', response_delay_sec=response_delay_sec)['payload']
        if not result:
            return None, None

        current_thread_id = result['current-thread-id']
        threads = result['threads']
        return threads, current_thread_id

    def switch_thread(self, thr_id):
        """ Tell GDB to switch to a specific thread, given its ID """
        self._gdbmi_run_cmd_get_one_response('-thread-select %s' % thr_id, 'done', 'result')

    @staticmethod
    def _gdbmi_filter_responses(responses, resp_message, resp_type):
        return list(filter(lambda rsp: rsp['message'] == resp_message and rsp['type'] == resp_type, responses))

    @staticmethod
    def gdb2freertos_thread_id(gdb_target_id):
        """ Convert GDB 'target ID' to the FreeRTOS TCB address """
        return int(gdb_target_id.replace('process ', ''), 0)
