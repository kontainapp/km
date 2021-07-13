#!hello_test_link arguments to test, should be one
#
#  Copyright © 2021 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
#
# Only the first line is looked at when passed to KM as a payload file
# The rest are ignored for now, but in the future we may add here config info
# or some build-in fun,  e.g.
snapshot_interval 15sec
snapshot_dir /mySnapshots
support_shell false
verbose (mmap|mem)
gdb_listen true

