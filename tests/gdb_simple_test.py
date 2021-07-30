#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

import gdb

FUNC = "km_fs_prw"
SPECIAL_FD = -2020
BUSY_MMAPS = 1
FREE_MMAPS = 2
TOTAL_MMAPS = 3
RET_SUCCESS = 0
ESPIPE = 29


def bp_handler(event):
    """
    Callback for gdb hitting a breakpoint on FUNC
    Used to check values of internal KM vars.
    For special fd, does the validation based on the request in the read buffer, and
    returns success/failure via the FUNC.
    For all other fds or other functions, this is a noop
    """

    if event.breakpoint.location == FUNC:
        gdb.write("Special bp hit\n")
        fd_val = gdb.parse_and_eval("fd")
        if fd_val == SPECIAL_FD:
            # Fetch data from command in 'buf'. Command format is "query,verbosity,expected_count", e.g. "2,1,12"
            frame = gdb.selected_frame()
            block = frame.block()
            value = gdb.lookup_symbol('buf', block)[0].value(frame)
            str_t = gdb.lookup_type('char').pointer()
            command = value.cast(str_t).string()
            query, verbosity, expected_count = [
                int(i) for i in command.split(',')]
            verbosity = int(verbosity)
            if verbosity == 0:
                print(f"""
                fd in: {fd_val}
                buf in: {command}
                query: {query}
                verbosity: {verbosity}
                expected_count: {expected_count}
                """)
            # Busy maps only query = 1, Free Maps only query = 2, Total = 3
            query_mmaps(query, verbosity, expected_count)
    gdb.execute("continue")


def register_bp_handler():
    """ Sets up bp_handler() """
    gdb.events.stop.connect(bp_handler)
    print('\nset handle\n')


def set_breakpoint():
    """ Sets breakpoint at function defined in global FUNC """
    gdb.Breakpoint(FUNC)
    print("breakpoint at:", FUNC)


def run():
    """ Runs gdb """
    gdb.execute("run")


def query_mmaps(query, verbosity, expected_count):
    """
    Compares expected_count to count_mmaps return, and returns RET_SUCCESS in FUNC
    for handling in mmap_tester.c, map_count()
    """
    if query == BUSY_MMAPS:
        mmap = gdb.parse_and_eval("&machine.mmaps.busy")
        ret = count_mmaps(mmap, verbosity)
    elif query == FREE_MMAPS:
        mmap = gdb.parse_and_eval("&machine.mmaps.free")
        ret = count_mmaps(mmap, verbosity)
    elif query == TOTAL_MMAPS:
        mmap = gdb.parse_and_eval("&machine.mmaps.free")
        ret = count_mmaps(mmap, verbosity)
        mmap = gdb.parse_and_eval("&machine.mmaps.busy")
        ret = ret + count_mmaps(mmap, verbosity)
    else:
        ret = expected_count + 1
        gdb.write("Invalid query: ", query)

    if ret == expected_count:
        gdb.execute(f"return {RET_SUCCESS}")
    else:
        # ENOPIPE ernno = 29
        gdb.execute(f"return -{ESPIPE}")


def count_mmaps(tailq, verbosity):
    """ Returns count of tailq, in this case the passed mmap tailq """
    tq = tailq["tqh_first"]
    le = tq["start"]
    count = 0
    while tq:
        count = count + 1
        if verbosity == 1:
            distance = tq['start'] - le
            print(
                f"{count}  {tq} next={tq['link']['tqe_next']} start={hex(tq['start']/4096)} size={hex(tq['size']/4096)} prot={tq['protection']} flags={tq['flags']} km_fl={tq['km_flags']['data32']} distance= {hex(distance/4096)} fn={tq['filename']}")
            le = tq["start"] + tq["size"]
        tq = tq["link"]["tqe_next"]
    return count


def print_tailq(tailq):
    """ Prints tailq """
    return count_mmaps(tailq, 1)


def in_gdb_notifier():
    gdb.parse_and_eval("in_gdb")


class print_mmaps (gdb.Command):
    """ Gdb custom command to print mmaps from mmap tailq """

    def __init__(self):
        super(print_mmaps, self).__init__("print-mmaps", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        a = gdb.parse_and_eval(arg)
        print_tailq(a)


print_mmaps()


class run_test (gdb.Command):
    """ Gdb custom command to run mmap test """

    def __init__(self):
        super(run_test, self).__init__("run-test", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        set_breakpoint()
        register_bp_handler()
        run()


run_test()
