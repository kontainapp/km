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
    subprocess.run([
        f"{NGINX_TOP}/auto/configure",
        "--prefix=/opt/nginx",
    ], cwd=NGINX_TOP)


# Build
subprocess.run([
    "make",
], cwd=NGINX_TOP)
