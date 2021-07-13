#!/usr/bin/env python3
#
# Copyright © 2019-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# Given a guest virtual address, print the slot numbers
# for the guest page tables.
import sys

val = int(sys.argv[1], 0)
print("2mb slot {}".format((val & 0x3fffffff) >> 21))
print("1gb slot {}".format((val & 0x7fffffffff) >> 30))
