# How to add a Extension Module containing .so files to statically linked python.km

External package with compiled (.so) modules can be added to statically linked python.km by converting package .o files into a .a library and then linking it into python.km. Each .a is registered with runtime via constructor mechanism, and *dlopen/dlsym* calls are then emulated by using the above registration.

The majority of needed steps are automated in `./build_extensions.sh`. *This document describes the process, tools and usage.*

## Configuration

There are 2 places where the "statically linked .so modules" are configured:

* `extensions/skip_builtins.txt` - the build converts all Python default .so modules  (e.g. math or cptypes) to dlstatic approach. However we do not want to pick up all of them ... skip_builtins.txt defines an exception list.
* `extensions/modules.txt` - this file controls which modules need to be pulled from github, built and added to python.km. The file comments explain the format


## Algorithm

### A few data points

* when converting Python Container to Kointainers , we will need to either lift existing installed modules and just generate python.km with their code compiled in, or we'll need to analyze pip requirements.txt (or simple pip freeze) and recreate the module install. This way or another, we need to pull modules source, built it and link in
* If there is dependency, and the related .a is not on the box, we wil manually find out where it is coming from, and will add code to build the dependency and save it for further use.
  * E.g. for libffi.a - the code finds out that it is needed, but we had to manually add steps to build it, to buildenv

### Automation - buildenv images, ./build_extensions.sh and link-km.sh

Buildenv image now create a log of python build,  picks up all default/builtin modules with .so and converts them to dlstatic. It then filters out the ones mentioned in `extensions/skip_builtins.txt`, and creates link file for ld to use.

On the next step, we call `../link-km.sh`. The link script will invoke `build_extensions.sh` which uses `extensions/modules.txt`to configure anf generate pneeded artifacts, and then runs run and links python.km.

*build_extensions.sh does the following:*

* Checks extensions/modules.txt and install modules (or packages) into cpython/Lib so they are available on the run time
* Checks if the package or module have `.so` files
* if they do use `.so`:
  * Pulls packages  from github to cpython/Modules
    * Builds them there using module's setup.py
    * Analyzes output and generates all needed files:
      * `dlstatic_km.mk` per each package - this file builds all needed for link
      * .c per each .so - with translation tables for dlsym() and registration for dlopen()
  * Compiles new files (using the generated makefile); which in turn will create the following:
    * .a file per each .so file with the content of .so
    * .o file per each .so file with registration and tables
    * .txt file for each package - this is ld-file (passed as `@file` to ld line) which linked in the package
  * Passes a list of to-be-linked packages to `link-km.sh` which generates python.km
  * copies .id files (one file per original.so; .id files contain MD5 signature for .so and are created earlier, when analyzing build log). The files are copied from build (Modules) to Lib (run time). id file is used when dlopen(file) needs to find file by uniqie id in registration list - and the ID is kept in file.km.id

## Using 'fake' dlopen in the payload

Our `dlsym()` does the same thing as the real one - retuns the address of the funtion to be called. `dlopen()` is faked by pre-linking, so it always behaves a if it is the second call to dlopen() of the same .so.
See dlstatic_km.c in runtime.

### More details on the  steps, files and makefile

* Multiple .so can have conflicting symbol names. e.g. python *numpy* has a few of those
  * The function names that are linked in the code are mangled to avoid conflicts. Since the symbolic names in the tables for dlsym are clear text, and that can overlap between different .so, the code using it tells the difference by handle returned from dlopen.
  * symnames can certainly overlap between different .so
  * We use md5digest of the freshly built .so as id for this .so
* There are different shared libraries with the same basename but different paths (sometimes even in the same package. e.g. python *falcon* has 2 *url.so*).
  * To avoid the conflict, we register both .so name and .so name + md5 digest, and during dlopen() will be looking for the latter only if the former is not unique.
* We add _km to symbols and .km patterns to files just to make it easier to grep
* We *Generate the following per .so*  (`base` is filename stripped of .so suffix e.g. libffmpeg for libffmpeg.so.
  * base.km.json file is created side by side with .so,  with metadata: so_name, md5digest, .o names, and symbols , FFU.
  * base.km.symmap file in the same place has mapping "symbol symbol_ID_km" (in objcopy format - one sym map per line, space separated)
  * base.km.symbols.c file with tables for individual module.so. This file also has global constructor to register the "base_ID" .so for statically resolved dlopen/dlsym
* The makefile does the following:
  * uses objcopy to preprocess .o files (replace symbols) based on base.km.symmap
  * compiles (`-c`) base.km.symbols.c file
  * builds .a for each .so
  * generates .txt file for linker

## TESTING

This was tested manually, mainly by linking misc. modules into python.km and running 'import module; print(dir(module))' to make sure all imported fine.
For numpy a few simple functions were called to make sure the call flow is good.

## TODO / KNOWN ISSUES

* dlopen(NULL) is missing. `extensions/prepare_extension.py --self python.km` would generate the registration .c file, but all functions from standard libs (e.g. libc) have to be blacked out before it is usable
* improve module info (modules.txt) - we should use a database (yaml?) with known modules/.so , including mapping to id when needed, rename, dependent packages, etc...
  * for some modules. names are converted, i.g. 'pillow' is PIL in Lib.
  * some .so are placed directly under Libs (not Libs/Module), e.g. greenlet, pvectorc, kiwi (kiwisolver)
* LD_LIBRARY_PATH and LD_PRELOAD need to be handled
* dlopen() flags need to be handled or at least reacted to. E.g. GLOBAL is not supported
* dlinfo()
* blacklists should be per module
  * get blacklist out of prepare_extension.py, and make it either auto-generated from -l. or better, simply special files in repo
* use requirments.txt and 'pip3 freeze' format to get the lists of modules
* dependent packages  CHECK is MISSING - e.g. django brings in tons of stuff, each has to be manually added to modules.txt as of now

Also:

* Add sqlite module
* test with something other than Python
* review generalization - what is Python specific, what needs to be separated
