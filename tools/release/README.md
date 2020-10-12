# Release Tools

This tool is designed to release artifacts of `km` into `km-releases`. A
detailed explaination of the requirements can be found
`${TOP}/docs/release.md`.

# Usage

While we can compile `release.go` into binary, it's easier to use `go run` directly.

```bash
go run release.go --help
```

## Github Personal Access Token (PAT)

This tool require a Github PAT to authenticate with Github APIs, and will
read it from `GITHUB_RELEASE_TOKEN` env variable.

# Testing

We need to verify the install process works as expected. Under
`$TOP/tools/release/tests`, we have two tests, one test the install process
locally and another test the install process on an VM on azure. Each script
are expected to run where the script is located.

To run the local test:
```bash
# Optional: make sure /opt/kontain is created with the right ownership
cd ${TOP}/tools/release/tests/test_release_local; ./test_release_local.py
```

To run the remote test:
```bash
# Optional: login into azure
# Optional: make sure there is a default ssh key generated under $HOME/.ssh/id_rsa
cd ${TOP}/tools/release/tests; ./test_release_remote.py
```

The same tests is also triggered as part of azure CI. See
`$TOP/azure-pipeline-release.yaml` for details.
