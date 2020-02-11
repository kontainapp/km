#!/usr/bin/env python3

import os
import subprocess

CURRENT_PATH = os.path.dirname(os.path.realpath(__file__))
NGINX_TOP = os.path.join(CURRENT_PATH, "nginx")
REPO = "git@github.com:nginx/nginx.git"
BRANCH = "branches/stable-1.16"

# Download the source
if not os.path.exists(NGINX_TOP):
    subprocess.run([
        "git",
        "clone",
        REPO,
        "-b",
        BRANCH,
    ])

# Set up the build environment
BUILDENV_IMAGE = "kontain/buildenv-nginx-alpine"

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

# Build

BUILDENV_CONTAINER_NAME = "kontain-nginx-payload-build"

# Start the building container
subprocess.run([
    "docker",
    "run",
    "--rm",
    "-it",
    "-d",
    "-v", f"{NGINX_TOP}:/nginx:Z",
    "-w", "/nginx",
    "--name", BUILDENV_CONTAINER_NAME,
    BUILDENV_IMAGE,
])


def nginx_build_docker_exec(cmd):
    docker_exec = [
        "docker",
        "exec",
        "-w", "/nginx",
        BUILDENV_CONTAINER_NAME,
    ]
    subprocess.run(docker_exec + cmd)


nginx_build_docker_exec([
    "./auto/configure",
    "--prefix=/opt/nginx",
])

nginx_build_docker_exec([
    "make",
])

subprocess.run([
    "docker",
    "stop",
    BUILDENV_CONTAINER_NAME,
])
