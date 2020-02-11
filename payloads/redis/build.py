#!/usr/bin/env python3

import os
import subprocess

CURRENT_PATH = os.path.dirname(os.path.realpath(__file__))
REDIS_TOP = os.path.join(CURRENT_PATH, "redis")
REPO = "git@github.com:antirez/redis.git"
BRANCH = "6.0"

# Download the source
if not os.path.exists(REDIS_TOP):
    subprocess.run([
        "git",
        "clone",
        REPO,
        "-b",
        BRANCH,
    ])

# Build
BUILDENV_IMAGE = "kontain/buildenv-redis-alpine"

check_buildenv_image = subprocess.run([
    "docker",
    "image",
    "ls",
    "-q",
    BUILDENV_IMAGE,
], stdout=subprocess.PIPE)

if check_buildenv_image.stdout == b'':
    subprocess.run([
        "docker",
        "build",
        "-t", BUILDENV_IMAGE,
        "-f", "buildenv.dockerfile",
        CURRENT_PATH,
    ])

subprocess.run([
    "docker",
    "run",
    "--rm",
    "-v", f"{REDIS_TOP}:/redis:Z",
    "-w", "/redis",
    BUILDENV_IMAGE,
    "make",
])
