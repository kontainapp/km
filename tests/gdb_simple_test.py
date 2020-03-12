# Copyright © 2018-2020 Kontain Inc. All rights reserved.

# Kontain Inc CONFIDENTIAL

# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.

import gdb
import pylint

func = "km_fs_prw"
total_count = 0
special_fd = -2020
busy_mmaps = '1'
free_mmaps = '2'
total_mmaps = '3'


def bp_handler(event):
    if event.breakpoint.location == func:
        gdb.write("Special bp hit\n")
        fd_val = gdb.parse_and_eval("fd")
        if fd_val == special_fd:
            t = str(gdb.parse_and_eval("(char*)buf"))
            address = gdb.parse_and_eval("buf")
            tsplit = t.split(",")
            tsplit = t.split('"')
            infosplit = tsplit[1].split(",")
            query = infosplit[0]
            verbosity = int(infosplit[1])
            expected_count = int(infosplit[2])
            if verbosity > 0:
                print(f"""
                fd in: {fd_val}
                buf in: {t}
                Address: {str(address)}
                query: {query}
                verbosity: {verbosity}
                expected_count: {expected_count}
                """)
            # Busy maps only query = 1, Free Maps only query = 2, Total = 3
            query_mmaps(query, verbosity, expected_count)

    # don't stop, continue
    gdb.execute("continue")


def register_bp_handler():
    gdb.events.stop.connect(bp_handler)
    print('\nset handle\n')


def set_breakpoint():
    gdb.Breakpoint(func)
    print("breakpoint at:", func)


def run():
    gdb.execute("run")

# Busy maps only query = 1, Free Maps only query = 2, Total = 3


def query_mmaps(query, verbosity, expected_count):
    inferior = gdb.selected_inferior()

    if query == busy_mmaps:
        mmap = gdb.parse_and_eval("&machine.mmaps.busy")
        ret = count_mmaps(mmap, verbosity)
    elif query == free_mmaps:
        mmap = gdb.parse_and_eval("&machine.mmaps.free")
        ret = count_mmaps(mmap, verbosity)
    elif query == total_mmaps:
        mmap = gdb.parse_and_eval("&machine.mmaps.free")
        ret = count_mmaps(mmap, verbosity)
        mmap = gdb.parse_and_eval("&machine.mmaps.busy")
        ret = ret + count_mmaps(mmap, verbosity)
        print("count: ", ret)
        print("expected_count: ", expected_count)
    else:
        ret = expected_count + 1
        gdb.write("Invalid query: ", query)

    if ret == expected_count:
        gdb.execute("return 0")
    else:
        gdb.execute("return -29")


def count_mmaps(tailq, verbosity):
    tq = tailq["tqh_first"]
    le = tq["start"]
    count = 0
    if verbosity == 1:
        print_tailq(tailq)
    while tq:
        count = count + 1
        tq = tq["link"]["tqe_next"]
    return count


def print_tailq(tailq):
    tq = tailq["tqh_first"]
    le = tq["start"]
    counter = 0
    while tq:
        counter = counter + 1
        distance = tq['start'] - le
        print(f"{counter}  {tq} start={tq['start']} next={tq['link']['tqe_next']} size={tq['size']} prot={tq['protection']} flags={tq['flags']} fn={tq['filename']} km_fl={tq['km_flags']['data32']} distance= {distance}")
        le = tq["start"] + tq["size"]
        tq = tq["link"]["tqe_next"]


def in_gdb_notifier():
    gdb.parse_and_eval("in_gdb")


class print_mmaps (gdb.Command):
    """Print MMAPS"""

    def __init__(self):
        super(print_mmaps, self).__init__("print-mmaps", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        a = gdb.parse_and_eval(arg)
        print("Arg = ", str(arg))
        print("A = ", str(a))
        print_tailq(a)


print_mmaps()


class run_test (gdb.Command):
    """MMAP verification from tests"""

    def __init__(self):
        super(run_test, self).__init__("run-test", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        set_breakpoint()
        register_bp_handler()
        run()


run_test()
