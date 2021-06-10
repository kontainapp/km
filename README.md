KM master [![Github Actions Pipeline](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml/badge.svg)](https://github.com/kontainapp/km/actions/workflows/km-ci-workflow.yaml)
[![Build Status for Normal CI](https://dev.azure.com/kontainapp/KontainMonitor/_apis/build/status/kontainapp.km?branchName=master)](https://dev.azure.com/kontainapp/KontainMonitor/_build/latest?definitionId=4&branchName=master)
Nightly Build [![Build Status for Nightly CI](https://dev.azure.com/kontainapp/KontainMonitor/_apis/build/status/kontainapp.km-nightly?branchName=master)](https://dev.azure.com/kontainapp/KontainMonitor/_build/latest?definitionId=5&branchName=master)
KKM Master [![Build Status for KKM CI](https://dev.azure.com/kontainapp/KontainMonitor/_apis/build/status/kontainapp.km-kkm?branchName=master)](https://dev.azure.com/kontainapp/KontainMonitor/_build/latest?definitionId=7&branchName=master)

# KM (Kontain Monitor) and related payloads for Kontain.app VMs

All terminology and content is subject to change without notice.

Kontain is the way to run container workloads "Secure, Fast and Small - choose 3". Kontain runs workloads with Virtual Machine level isolation/security, but without any of VM overhead - in act, we generate smaller artifacts and faster startup time than "classic" Docker containers.

We plan to be fully compatible with OCI/CRI to seamlessly plug into Docker/Kubernetes run time environments

For one page Kontain intro, Technical Users whitepapers and info see Kontain's [Google docs](https://docs.google.com)

:point_right: **For build and test info, see [docs/build.md](docs/build.md).**

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

We use Visual Studio Code as recommended IDE; install it and use `code km_repo_root/.vscode/km.code-workspace` to start, or use `File->Open Workspace` and open  **.vscode/km.code-workspace** file

## Build environment

[docs/build.md](docs/build.md) has the details, but if you want to get started right away:

* Make sure you have 'az' CLI installed

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

* Detailed document  for coding style is still TBD, below is a high level outline
* The majority of formatting is handled by Visual Studio Code.
  * We use clang-format forcing to be compliant with tabs and the likes. **Do not forget to `sudo dnf install clang`** (or install clang with whatever package manager you are using)
  * KM workspace has "Format on Save" turned on, please do not turn it off in your User settings.
  * **If you prefer to edit the source using other tools**, you *MUST* enable git pre-commit hooks (`make git-hook-init`) to auto-format on commit.
* Generally, we pay attention to the code being easy to read.
  * Give short (but meaningful) names to all, do not save on comments, and try to stay within the look and feel of the other code.
  * Try to limit function / methods / blocks size to a readable page (50-60 lines). "50-60" is not a hard requirement, but please do not create multi-page functions.
* Single line comments are `//`, multiple lines `/* ... */`
* We use km_underscore_notation for vars and functions.
* Never use single lines `if (cond) do_something`. *Always* use `{}` - i.e.

```C
if (cond) {
   do something;
}
```

* we always compare function results with explicit value. E.g. we do `if (my_check() == 0) ...` instead of `if (!my_check())...`
* for `return` statement, if we return a single token (with or without sign), then we do not use `()`, otherwise we do. E.g. `return 0;` and `return -ENOMEM;`, but `return (value + 1);`
* Don't declare multiple variable on the same line, particularly when there are initial values assigned.
* Don't assign multiple variables on the same line. Instead of `a = b = 0;` use two separate assignments.

### The following isn't hard requirement but a strong preference

* We like the code to be compact, for instance we like

```C
if ((rc = function()) < 0) {
   do something;
}
```

Or, if applicable

```C
for (int index = 0; index < MAX_INDEX; index++) {
   loop body;
}
```

* We like variable declaration closer to the place in the function where they are used, instead of more traditional way to put all declarations at the top of a function.

## Test runs

See `docs/testing.md` for details. A few hints

* in Bash, `make <tab>` tells you available targets
* You can also run `make help` for more info
* `make test MATCH=regexp` will build and only run tests matching regexp
* tests/*.bats files is were the test control is

## Debugging payloads

See the Debugging section of `docs/testing.md` for an introduction to debugging km payloads.

## CI/CD

We use Azure DevOps pipelines. See [docs/azure_pipeline.md](docs/azure_pipeline.md)
