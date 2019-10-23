# How to add a (compiled) Extension Module to python.km

External package with compiled (.so) modules can be added to python.km by building the packges into .o files, and linking with python.km.
During this process we add the module as a built-in Python module, and during import we intercept the request and provide the correct built-in module name.

The majority of needed steps are automated in `payloads/python/build.sh`. Here is how to prepare the modules for build.sh

## Add module's code to python.km as a built-in module

1. **Get the Package Source Code into cpython/Modules**, i.e. `cd cpython/Modules; git clone package_src_repo`.
1. **Add module names and version to `m_list` and `m_version arrays` in `payloads/python/build.sh`. (going forward we'll make these lists external and auto-populate when analyzing the app being converted)
1. **run payloads/python/build.sh**. It will build the module using module's setup.py, and prepare new python.km file with new built-in module.

**NOTE** The final phase (.km link) may fail if new modules require extra `-l`. See next step if that happens.

## Update and re-run `km-link.sh` if needed

This step is not automated yet (but will be)
Check `-l` and `-L` options in  the Modules/your_module/`km_libs.json` files, if there are any - add them to the link line in `python/km-link.sh`, and re-run it.

## Install the actual module so python can find it

For example, when running python.km from build directory you can do `pip3 install -t cpython/Lib package_name`.

If you use PYTHONHOME or PYTHONPATH, just make sure the package is accessible to python.km (assumung you pass the env to KM via `--putenv`)

## Check Site customize

If your python.km is NOT located in python build tree (cpython), you need to make sure km_sitecustomer.py is copied as `sitecustomize.py` to site-specific (i.e. anywhere in sys.path) location.

Note that it's all taken care of by `build.sh` when you run cpython/python.km

