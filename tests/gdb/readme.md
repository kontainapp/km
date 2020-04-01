mmaps_count(expected_count, query)
Passes expected_count and query (BUSY_MMAPS, FREE_MMAPS, TOTAL_MMAPS) to gdb and tests the accuracy of the mmaps in km.

gdb is notified via a specific fd passed during read/write (read currently being used)

This is skipped over entirely outside of KM, and if not in_gdb, where it prints a request to be ran in gdb or KM

To add another option to be tested, add extra input into mmaps_count, and pass it into the read_check_result buffer. 

Parsed through in gdb_simple_test.py, add an extra check and grab of input similar to how expected_count is passed. 