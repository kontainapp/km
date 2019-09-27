# Extension Module Instructions

Preparing a external package with Python extensions for use by KM.
## Preparing a package for KM

### Step 1: Get the Package Source Code
Run `git clone` for package source from directory `cpython/Modules`.

### Step 2: Change Directory to Package Source
Run `cd <pkgname>`

### Step 3: Build the Package and Capture Output
Run `bear python3 setup.py build > bear.out`. This creates the files `compile_commands.json` and `bear.out`.

### Step 4: Extract Build Information
Run `extract_build.py` (creates `km_capture.km`)

### Step 5: Get Python specific information
Run `build_pyext.py`(creates `km_setup.local`, `km_libs.json`, and `.c` files with munged `PyInit_` function names)

### Step 6: Archive the Convereted Package
Run `cd ..; tar czf <pkgname>.tgz <pkgname>`

## Creating a Application Specific `python.km`

### Step 1: Unarchive Needed Packages
Inside the directory `cpython/Modules` run `tar xf <pkgname>.tgz`.

### Step 2: Create new `Setup.local`
Append the `km_setup.local` files from the package directory(ies) to KM's default `Setup.local`.

### Step 3: Create new `km-link.sh`
Add the options in the package's `km_libs.json` to the link line.

### Step 4: Build it
Run `build.sh`

### Step 5: Install `.py` Files
For each add-on package: `cd cpython/Lib; ln -s ../Modules/<pkgname>/<pkgname> <pkgname>` (There is probably a way to do this with params to the package's `setup.py`).

## Running under KM

The file `payloads/python/extensions/km_sitecustomize.py` must be in the Python site directory as `sitecustomize.py.
