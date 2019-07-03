# KM (Kontain Monitor) and related payloads for Kontain.app VMs

All terminology and content is subject to change without notice.

Kontain is the way to run container workloads "Secure, Fast and Small - choose 3". Kontain runs workloads with Virtual Machine level isolation/security, but without any of VM overhead - in act, we generate smaller artifacts and faster startup time than "classic" Docker containers.

We plan to be fully compatible with OCI/CRI to seamlessly plug into Docker/Kubernetes run time environments

For one page Kontain intro, Technical Users whitepapers and info see Kontain's [Google docs](https://docs.google.com)

For build and test info, see [docs/build.md](docs/build.md).

## Files layout

* `docs` is where we keep documents and generally, keep track of things.
* `km` is the code for Kontain Monitor
* `tests` is obviously where the test are
* `make` are files supporting build system, and usually included from Makefiles
* `runtime` is the libraries/runtime for KM payloads. e.g. musl C lib
* `payloads` is where we keep specific non-test payloads, e.g. `python.km`
* `cloud` is the code for provision cloud environments (e.g. azure) to run containerized KM payloads

`README.md` files in these directories have more details - always look for README.md in a dir. If it's missing, feel free to create one and submit a PR to add it to the repo.

## Getting started

To get started, read the introduction and technical user whitepaper, then build and test and go from there.

We use Visual Studio Code as recommended IDE; .vscode/km.code-workspace is the workspace defintion to open with File/OpenWorkspace there.

## Contributing

* Create a PR with `your-name/branch-name` branch. Try to give meaningful names, rather that "issueNN"
* Make sure the code style is compliant with the rest (see next section)
* Make sure you run `make test` before submitting. (this step will be optional once we add CI/CD)
  * optional - try `make withdocker DTYPE=ubuntu TARGET=test` to validate it is still good on Ubuntu
  * it is also a good idea to run `make coverage` to check if your new code is covered by tests
* In the PR:
  * Give it a clear title.
  * Add `Closes #xx` (e.g. `Closes #12`) to Description if it closes an issue. This way  github will auto-close the issue when PR is merged
  * Add info to description to help reviewers to understand why it is done / what is done and how it was tested
  * Ideally, add a test which fails without the change and passes with the change

## Code style

* Our coding style doc is still TBD
* The majority of formatting is handled by Visual Studio Code.
  * We use clang-format forcing to be compliant with tabs and the likes.
  * KM workspace has "Format on Save" turned on, please do not turn it off in your User settings.
* Generally, we pay attention to the code being easy to read. Give short (but meaningful) names to all, do not save on comments, and try to stay within the look and feel of the other code.
* Single line comments are `//` , multiple lines `/* ... */`
* We use km_underscore_notation for vars and functions.
* Never use single lines `if (cond) do_something`. *Always* use `{}` - i.e.

```
if (cond) {
   do something;
}
```

## Test runs

See `docs/testing.md` for details. A few hints

* in Bash, `make <tab>` tells you available targets
* You can also run `make help` for more info
* `make test MATCH=regexp` will build and only run tests matching regexp
* tests/*.bats files is were the test control is

## CI/CD

TBD - work in progress
