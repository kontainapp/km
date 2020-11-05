# Release Tools

This tool is designed to release artifacts of `km` into `km-releases`. A
detailed explanation of the requirements can be found in
`${TOP}/docs/release.md`.

## Usage

Install dependencies:

```bash
pip install -r requirement.txt
```

If the artifacts released is under `${TOP}/build/kontain.tar.gz`, to run:

```bash
./release_km.py ${TOP}/build/kontain.tar.gz --version v0.1-test
```

For other usages:

```bash
./release_km.py -h
```

Note: version `v0.1-test` is a special tag reserved for testing. The release
script will try to clean up when this release tag exists. Otherwise, the
release script will fail to prevent accidental override of existing releases.

## Github Personal Access Token (PAT)

This tool require a Github PAT to authenticate with Github APIs, and will
read it from `GITHUB_RELEASE_TOKEN` env variable. See Github's help on "Creating a personal access token."

When using from CI, the release token is pre-set as a pipeline variable

## Testing

We need to verify the install process works as expected. Under
`$TOP/tools/release/tests`, we have two tests, one test the install process
locally and another test the install process on an VM on azure. Each script
are expected to run where the script is located.

To run the local test:

```bash
# Optional: make sure /opt/kontain is created with the right ownership
cd ${TOP}/tools/release/tests/test_release_local; ./test_release_local.py --version v0.1-test
```

To run the remote test:

```bash
# Optional: login into azure
# Optional: make sure there is a default ssh key generated under $HOME/.ssh/id_rsa
cd ${TOP}/tools/release/tests; ./test_release_remote.py --version v0.1-test
```

The same tests is also triggered as part of azure CI. See
`$TOP/azure-pipeline-release.yaml` for details.
