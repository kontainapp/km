# How to add a (compiled) Extension Module to python.km

## Using Python built-in module mechanism

External package with compiled (.so) modules can be added to python.km by building the packges into .o files, and linking with python.km.
During this process we *add the module as a built-in Python module, and during import we intercept the request and provide the correct built-in module name*.

The majority of needed steps are automated in `payloads/python/build.sh`. Section below summarize what build.sh does

### Add module's code to python.km as a built-in module

1. **Get the Package Source Code into cpython/Modules**, i.e. `cd cpython/Modules; git clone package_src_repo`.
1. **Add module names and version to `m_list` and `m_version arrays` in `payloads/python/build.sh`. (going forward we'll make these lists external and auto-populate when analyzing the app being converted)
1. **run payloads/python/build.sh**. It will build the module using module's setup.py, and prepare new python.km file with new built-in module.

**NOTE** The final phase (.km link) may fail if new modules require extra `-l`. See next step if that happens.

### Update and re-run `km-link.sh` if needed

This step is not automated yet (but will be)
Check `-l` and `-L` options in  the Modules/your_module/`km_libs.json` files, if there are any - add them to the link line in `python/km-link.sh`, and re-run it.

### Install the actual module so python can find it

For example, when running python.km from build directory you can do `pip3 install -t cpython/Lib package_name`.

If you use PYTHONHOME or PYTHONPATH, just make sure the package is accessible to python.km (assumung you pass the env to KM via `--putenv`)

### Check Site customize

If your python.km is NOT located in python build tree (cpython), you need to make sure km_sitecustomer.py is copied as `sitecustomize.py` to site-specific (i.e. anywhere in sys.path) location.

Note that it's all taken care of by `build.sh` when you run cpython/python.km

## Using 'fake' dlopen in the payload

Objects are still linked in the payload.
dlopen/dlsym called from the payload would be routed to our code (not musl) which scans symbols tables and fakes dynamic dlsym by returning link-time defined addresses.

This part is work in progress and will replace the "built-in module" part. The first pass is python-specific and uses python packages/modules to uniquely id the .so files.

`build.sh` will automate the following steps:

* **python specific** read modules.txt with extension modules list, pull source from github, build extensions (we need .o file and .so file from there)
* look at the output of the build and  for for each .so file get (1) a list of symbols exported and (2) a list of .o files
* Add unique id to be used - md5digest for the .so content
  * .so can have the same name in different dirs (e.g. python *falcon* has 2 *url.so*.
  * symnames can certainly overlap between different .so (e.g. python *numpy* has 5 *xerbla* symbols.
* prep the mapping the .so name to base_md5Digest_km and symbols to to "symbol_md5Digest_km", to avoid internal clashes.
  * We use md5Digest as `ID`.
  * we add _km just to make it easier to grep
* *Generate the following per .so*  (`base` is filename stripped of .so suffix e.g. libffmpeg for libffmpeg.so.
  * base.km.json file is created side by side with .so,  with metadata: so_name, md5digest, .o names, and symbols , FFU.
  * base.km.symmap file in the same place has mapping "symbol symbol_ID_km" (in objcopy format - one sym map per line, space separated)
  * base.km.symbols.c file with tables for individual module.so. This file also has global constructor to register the "base_ID" .so for statically resolved dlopen/dlsym
* generate one km_libs.mk makefile per **python module**. For non-python cases, the .mk file will probably be per .so.
* The makefile does the following:
  * uses objcopy to preprocess .o files (replace symbols) based on base.km.symmap
  * compiles (`-c`) base.km.symbols.c file
  * builds .a for each .so
*  links the result by passing .o files explicitly and .a files as -l

`dlopen` code will (**python specific !**) check the location of the file and construct unique name ("package_module"). Use this internally as a handle key to resolve symbols

the plan is to have all dlopen/dlsym related code is in payload only. Nothing in KM nor gcc-kontain / spec scripts

Open issues:

* .so referring to another .so .. how to mangle refs ?!! (no examples. can ignore for now). as long as the undefined are in the same .so. all works as intended
