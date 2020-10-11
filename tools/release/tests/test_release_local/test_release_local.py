#!/usr/bin/env python3

import os
import tempfile
import shutil
import subprocess

OPT_KONTAIN = "/opt/kontain"
OPT_KONTAIN_BIN = f"{OPT_KONTAIN}/bin"
KONTAIN_GCC = f"{OPT_KONTAIN_BIN}/kontain-gcc"
KM = f"{OPT_KONTAIN_BIN}/km"

# Clean up the /opt/kontain so we have a clean test run
subprocess.run(["rm", "-rf", f"{OPT_KONTAIN}/*"])

# Download and install
os.system("wget https://raw.githubusercontent.com/kontainapp/km-releases/master/kontain-install.sh -O - -q | bash")

# Test: compile helloworld with kontain-gcc
work_dir = tempfile.mkdtemp()
shutil.copytree("assets", os.path.join(work_dir, "assets"))
subprocess.run([
    KONTAIN_GCC,
    os.path.join(work_dir, "assets", "helloworld.c"),
    "-o",
    "helloworld",
], cwd=work_dir)
subprocess.run([
    KM,
    "helloworld",
], cwd=work_dir)

# Clean up
shutil.rmtree(work_dir)
