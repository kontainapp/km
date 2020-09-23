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