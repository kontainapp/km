# How to add a Extension Module containing .so files to statically linked python.km

External package with compiled (.so) modules can be added to statically linked python.km by converting package .o files into a .a library and then linking it into python.km. Each .a is registered with runtime via constructor mechanism, and *dlopen/dlsym* calls are then emulated by using the above registration.

The majority of needed steps are automated in the Makefile. *This document describes the process, tools and usage.*

## Getting help

The code is in `extensions`. All scripts usually have `--help` option. Make has `help` target.
Other than that, read this doc and then the code

## For the impatient

If you want to check out a custom build with pre-defined modules, you can do the following:

1. Use `extension/analyze_modules.sh "module_names"` to get info from Google and github about modules.
1. Check the version is populated there, and manually add the modules to extensions/modules.json (or pass extensions/modules.json to the script above as 2nd arg)
1. Run `make build-modules MODULES="module_names"`
1. Run `make custom  CUSTOM_MODULES="module_name" CUSTOM_NAME=tryout`

A few details:

* Modules have to have metadata in `extensions/modules.json` - it is what scripts and make are using for getting git URL and other stuff.
* Modules are build with `make build_modules`.
  * It clones the module repo to cpython/Modules, compiles the modules, generates Kontain libs and tables, and then packs the result in a local docker image (the last step can be done separately with `make pack-modules`).
* The image can be uploaded to dockerhub and pulled back via `make push-modules` and `make pull-modules` targets.
  * All repos are public and do not require authentication for pull
  * push-modules requires docker credentials for dockerhub, expected in in ~/.docker/username and ~/.docker/token
* Once the modules are either built or pulled, they can be referred from `extension/python-custom.json` and custom km can be built with `make custom`.
  * *custom* is the default for CUSTOM_NAME make variable.
  * also instead of file with custom definition, you can pass `CUSTOM_MODULES="module_names" CUSTOM_NAME=some_name` and the `python-some_name.km` will be made.

## Configuration

We use following configuration files:

* `extensions/skip_builtins.txt` - a list of built-in .so modules which we want to *skip when building python.km*.
* `extensions/modules.json` - information about modules we looked at or tried, and certainly about all modules we've successfully built. Self-described json format.
* `extension/python-custom.json` - list of modules to add to custom configuration. Note that multiple custom configurations can exist, e.g. python-numerical.json, or whatever. python-custom.json is just an example and default to pick up

## Algorithm

### A few data points

* When converting Python Container to Kontainers, we will need to do the following:
  * get a list of existing installed modules (or .so files) from the original container.
  * pull in docker container with the Kontain-specific object files for those .so
  * build custom python.km
* To "pre-create Kontain-specific object files", we need to do the following:
  * find out where the source it
  * pull it in modules source and build it
  * convert it to Kontain objects, and save for future use
* If there is dependency, and the related .a is not on the box (e.g libffi.a), we manually find out where it is coming from, and manually add code to build the dependency and save it for further use.
  * E.g. for libffi.a - the code finds out that it is needed, but we had to manually add steps to build it, to buildenv

### Automation - buildenv images, ./build_modules.sh and link-km.sh

Buildenv image for python now places built-in .so through the conversion and linkes them into python.km

* It creates a log of cpython build
* picks up all default/builtin modules with .so and converts them to dlstatic.
* filters out the ones mentioned in `extensions/skip_builtins.txt`, and creates link file for ld to use.

`make custom` should be used to build a custom python.km. It uses the following:

* a list of modules to link in. See `extension/python-custom.json` for example. Accepts `CUSTOM_NAME=name`. Default is `custom`
* pre-built modules. See next section

We pass the extra files to link to `../link-km.sh`.

### Python pre-build modules - make targets and scripts

See 'make help | grep modules' in python folder. Basically

* `build-modules` to git clone and build all modules mentioned in modules.json as "validated". E.g. `make build-modules MODULES="pyrsistent markupsafe"`
* `pack-modules` to pack into local docker images
* `push-modules` pushes them to dockerhub (requires login)
* `pull-modules` pulls and unpack so `make custom` can build the KM

We clone and build original modules, and save the build log. We then (in `prepare_extension.py`) analyze output and generate all needed files:

* `dlstatic_km.mk` per each package - this file builds all needed for link
* .c per each .so - with translation tables for dlsym() and registration for dlopen()

We then compile new files (using the generated makefile); which in turn will create the following:

* .a file per each .so file with the content of .so
* .o file per each .so file with registration and tables
* .txt file for each package - this is ld-file (passed as `@file` to ld line) which linked in the package

The .txt file can be passed to `link-km.sh` which will link the required module in

### How to deal with new modules

If a payload uses modules we have not looked at yet, module definition can be partially build using `analyze_modules.sh`


## Using 'fake' dlopen in the payload

Our `dlsym()` does the same thing as the real one - returns the address of the function to be called. `dlopen()` is faked by pre-linking, so it always behaves a if it is the second call to dlopen() of the same .so.
See dlstatic_km.c in runtime.

### More details on internals

* Multiple .so can have conflicting symbol names. e.g. python *numpy* has a few of those
  * The function names that are linked in the code are mangled to avoid conflicts. Since the symbolic names in the tables for dlsym are clear text, and that can overlap between different .so, the code using it tells the difference by handle returned from dlopen.
  * symnames can certainly overlap between different .so
  * We use md5digest of the freshly built .so as id for this .so
* There are different shared libraries with the same basename but different paths (sometimes even in the same package. e.g. python *falcon* has 2 *url.so*).
  * To avoid the conflict, we register both .so name and .so name + md5 digest, and during dlopen() will be looking for the latter only if the former is not unique.
* We add _km to symbols and .km patterns to files just to make it easier to grep
* We *Generate the following per .so*  (`base` is filename stripped of .so suffix e.g. libffmpeg for libffmpeg.so.
  * base.km.json file is created side by side with .so,  with metadata: so_name, md5digest, .o names, and symbols, FFU.
  * base.km.symmap file in the same place has mapping "symbol symbol_ID_km" (in objcopy format - one sym map per line, space separated)
  * base.km.symbols.c file with tables for individual module.so. This file also has global constructor to register the "base_ID" .so for statically resolved dlopen/dlsym
* The generated makefile does the following:
  * uses objcopy to pre-process .o files (replace symbols) based on base.km.symmap
  * compiles (`-c`) base.km.symbols.c file
  * builds .a for each .so
  * generates .txt file for linker

## TESTING

This was tested manually, mainly by linking misc. modules into python.km and running 'import module; print(dir(module))' to make sure all imported fine.
For numpy a few simple functions were called to make sure the call flow is good.

## TODO / KNOWN ISSUES

This is a working section and is intended to keep track of work  and issues without polluting github issues section

### python package handling

* recursive dependencies analysis is missing. Specifically, nested packages may have .so and need to be prepped
* Need to add static .a for -lxx flags directly to docker images, and fix -L lines, to make build more self-contained
* Need test of handling of packages which install in differently named folders (e.g. greenlet).
* run test over custom KM, including new modules test
  * Generally, packages are built but not tested. Numpy is tested, the rest is not really.
  * test is manual (not good)
* analyze_modules often fails to get version: github anonymous API limit rate is 60 hits/hr/client, so so version info may fail to fetch
* versions in custom config are essentially ignored. Generally, version are not paid much attention to yet

minor inconvenience:

* build-modules is rebuilding all, maybe just skip if module already there and add either FORCE=yes or flag file ?
  * same for pack-modules, but it's fast

### dlopen/dlsym

* dlopen(NULL) and dlsym(NULL) is missing. `extensions/prepare_extension.py --self python.km` would generate the registration .c file, but all functions from standard libs (e.g. libc) have to be blacked out before it is usable
* need weak aliases for original symbols. for dlopen(NULL) and potential linkage problems when the original symbol is referred
* dlopen() flags need to be handled or at least reacted to. E.g. GLOBAL is not supported
* dlinfo() is noop now

### Faktory

OPEN:

* how to test the results?

KNOWN:

* [todo].ids need to be extracted during factory work - make sure .ids are in the images
  * note: .id files contain MD5 signature for .so and are created earlier, when analyzing build log). The files are copied from build (Modules) to Lib (run time). id file is used when dlopen(file) needs to find file by unique id in registration list - and the ID is kept in file.km.id
* use requirements.txt and 'pip3 freeze' format to get the lists of modules in faktory

--- END OF FILE ---
