#!/usr/bin/env python3
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
# This file includes unpublished proprietary source code of Kontain Inc. The
# copyright notice above does not evidence any actual or intended publication
# of such source code. Disclosure of this source code or any related
# proprietary information is strictly prohibited without the express written
# permission of Kontain Inc.

""" release_km

    Build a github release for <tag> in `kontainapp/km` repo
    Usage:  ./release_km.py <asset file(s)...> --version  <tag> --message "<release message>"
"""

import argparse
import os
import sys
import logging
import time

try:
   import github
except:
   print("Error: Missing PyGitHub module")
   sys.exit(2)

RELEASE_REPO_OWNER = "kontainapp"
RELEASE_REPO_FULLNAME = "kontainapp/km"
# Use this is the version name is not compliant with expectations
RELEASE_DEFAULT_VERSION = "v0.1-test"
# Delete these if they already exist
OVERRIDABLE_RELEASES = [ RELEASE_DEFAULT_VERSION, "v0.1-beta", "v0.1-edge" ]

def main():
    """ main """

    parser = argparse.ArgumentParser()
    parser.add_argument("--version", help="version of km to be released")
    parser.add_argument("--message", default="", help="release message for km")
    parser.add_argument("assets", nargs='*', help="assets for the new release")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger()

    # GITHUB_RELEASE_TOKEN is required to get access to km-releases repo. The
    # token is the Github Personal Access Token (PAT)
    token = os.environ.get("GITHUB_RELEASE_TOKEN")
    if token is None:
        raise ValueError("GITHUB_RELEASE_TOKEN is not set")

    if args.version is not None:
        version = args.version
    else:
        version = RELEASE_DEFAULT_VERSION

    # If invoked from azure pipeline, the tag name has the full reference name.
    # We will need to remove it for API calls.
    if version.startswith("refs/tags/"):
        version = version[len("refs/tags/"):]

    if not version.startswith("v"):
        logger.warning("Triggered by a non-standard version. Using default test version %s",
                       RELEASE_DEFAULT_VERSION)
        version = RELEASE_DEFAULT_VERSION

    client = github.Github(token)
    release_repo = client.get_repo(RELEASE_REPO_FULLNAME)

    # Github releases require an unique name. If the version is in overridables,
    # we will delete the release, no questions asked.
    # Otherwise, we will error out.
    try:
        release = release_repo.get_release(version)
    except github.UnknownObjectException:
        # 404 indicating no release with this version name has been created. This is expected.
        pass
    else:
        if version not in OVERRIDABLE_RELEASES:
            raise ValueError(f"Release {version} already exist...")
        logger.info(f"Override release {version}")
        release.delete_release()

    # Create a reference and a release based on the reference. Also upload any
    # assets to the release if needed.
    created_release = None
    try:
      created_release = release_repo.create_git_release(
            version, f"Kontain {version}", args.message, draft=False, prerelease=True)

      for asset in args.assets:
            uploaded_asset = created_release.upload_asset(asset)
            logger.info("Successfully uploaded %s url: %s",
                        uploaded_asset.name, uploaded_asset.url)
    except github.GithubException:
        logging.error("Failed to release... cleanup now...")
        # If any of the release process fails, try to clean up.
        if created_release is not None:
            created_release.delete_release()
        raise

    logging.info(f"Release {version} created")

main()
