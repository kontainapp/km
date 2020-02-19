import gdb

func = "km_fs_prw"
total_count = 0


def bp_handler(event):
    if event.breakpoint.location == func:
        i = gdb.selected_inferior()
        print("_____________________")
        gdb.write("Special bp hit\n")
        s = gdb.parse_and_eval("fd")
        if s == -2020:
            t = str(gdb.parse_and_eval("(char*)buf"))
            address = gdb.parse_and_eval("buf")
            tsplit = t.split(",")
            tsplit = t.split('"')
            infosplit = tsplit[1].split(",")
            query = infosplit[0]
            verbosity = infosplit[1]
            expected_count = infosplit[2]
            if int(verbosity) > 0:
                print("fd in: ", s)
                print("buf in: ", t)
                print("Address: ", str(address))
                print("query: ", query)
                print("verbosity: ", verbosity)
                print("expected_count: ", expected_count)

            if query == '1':
                r = gdb.parse_and_eval("&machine.mmaps.busy")
                x = str(print_tailq(r, verbosity, expected_count))
                i.write_memory(address, x)
            elif query == '2':
                r = gdb.parse_and_eval("&machine.mmaps.free")
                x = str(print_tailq(r, verbosity, expected_count))
                i = gdb.selected_inferior()
                i.write_memory(address, x)

            elif query == '3':
                r = gdb.parse_and_eval("&machine.mmaps.free")
                x = str(print_tailq(r, verbosity, expected_count))
                r = gdb.parse_and_eval("&machine.mmaps.busy")
                x = str(print_tailq(r, verbosity, expected_count))
                i.write_memory(address, x)
                total_count = 0
            else:
                i.write_memory(address, "invalid query")

    # don't stop, continue
    gdb.execute("return -29")
    gdb.execute("continue")


def register_bp_handler():
    gdb.events.stop.connect(bp_handler)
    print('\nset handle\n')


def set_breakpoint():
    gdb.Breakpoint(func)
    print("breakpoint at:", func)


def run():
    gdb.execute("run")
    # gdb.execute("handle SIGUSR1 nostop")
    # gdb.execute("c")


def print_tailq(tailq, verbosity, expected_count):
    tq = tailq["tqh_first"]
    le = tq["start"]
    count = 0
    while tq:
        count = count + 1

        if int(verbosity) == 1:
            distance = tq['start'] - le
            print(f"{count}  {tq} start={tq['start']} next={tq['link']['tqe_next']} size={tq['size']} prot={tq['protection']} flags={tq['flags']} fn={tq['filename']} km_fl={tq['km_flags']['data32']} distance= {distance}")
            le = tq["start"] + tq["size"]
            print("Count: " + str(count) +
                  "  |  Expected Count: " + str(expected_count))

        tq = tq["link"]["tqe_next"]
    if total_count > 0:
        count = count + total_count
    if int(expected_count) == count:
        return True
    return False


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
        print_tailq(a, 1, 0)


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
