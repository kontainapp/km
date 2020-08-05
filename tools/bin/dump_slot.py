#!/usr/bin/env python3
#
# Given a guest virtual address, print the slot numbers
# for the guest page tables.
import sys

val = int(sys.argv[1], 0)
print("2mb slot {}".format((val & 0x3fffffff) >> 21))
print("1gb slot {}".format((val & 0x7fffffffff) >> 30))
