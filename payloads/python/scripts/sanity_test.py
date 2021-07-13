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
# Very basic sanity checks for python in KM
import os
import _ctypes

# If we are here, we run ok-ish
if os.uname().release.find("kontain") == -1:
   raise BaseException(f"We don't seem to be running under Kontain Monitor.\n{os.uname()}")

# make sure embedded modules (at least _ctypes)
if '_objects' not in dir(_ctypes.Union):
   raise BaseException(f"Error in built-in load: _objects not found in _ctypes.Union.\n{dir(_ctypes.Union)}")

print("Hello from python in Kontain VM: sanity check passed")
