# KM (Kontain Monitor) and related payloads for Kontain.app VMs.

All terminology and content is subject to change without notice.

For build and test info, see ./docs/build.md.

* Generally, `docs` is where we keep track of things.
* `km` is the code for Kontain Monitor
* `tests` is obviously where the test are
* `make` are files suporting build system, and usually included from Makefiles
* `runtime` is the libraries/runtime for KM payloads. e.g. musl C lib
* `payload` is where we keep specific non-test payloads, e.g. `python.km`
* `cloud` is the code for provision cloud environments (e.g. azure) to run containerized KM payloads
