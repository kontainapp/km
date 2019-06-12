# Test and CI planning

Doc version: 0.1
Date: 6/12/2019

This is a working doc for CI and test /test infra changes.
We will also keep planning, design and write ups here, when available.

See `testing.md` file for the current testing support info

## Goals

* Automatically run in CI pipeline on pushes
* Maintain good test coverage (including errors) - say 90% code 80% branches
* Run sanitizers/valgrind in test passes
* Run payload-specific (e.g. python) tests in CI

## Work items

* Make sure what we have is passing - all permutations (e.g. `make withdocker TARGET=test DTYPE=ubuntu`)
* CI: build and test run on each push to PR branch.
  * Test run should include all permutations of tests we already have
  * Working assumption - use Azure Pipelines, to simplify life
     * Requirements: should be able to run containers with KVM, or run on Azure VMs directly
* Bring test coverage up - cover error cases.
  * Pick up and implement framework for this
* Add ./payload tests
  * either grow test incrementally, or start from existing python tests and config to ignore what we don't support
  * basics - maybe using the demo
  * python coverage, runtime libs coverage, etc
* decide if we need different/better test framework

