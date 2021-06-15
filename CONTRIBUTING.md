# Contributing

## Steps

* Create a PR with `your-name/branch-name` branch. Try to give meaningful names, rather that "issueNN"
* Make sure the code style is compliant with the rest (see next section)
* Make sure you run `make test` before submitting.
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

We use GitHub Actions to control CI workflow, see .github/workflows for configuration.

- The actual steps are all implemented in Make system in the repo, and can be used in any CI or manually - which is a recommended way to use them to troubleshoot CI steps.
- The build is happending on CI machines. Test steps use resources from AWS, Azure and Azure Kubernetes Service to validate builds.
