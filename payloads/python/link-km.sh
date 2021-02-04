#!/bin/bash
#
# Links Python.km from python libs and km 'dlstatic' builditns mentioned in $target/linkline_km.txt
#
set -e ; [ "$TRACE" ] && set -x

if [ "$1" == "--help" ] ; then
  cat << EOF
Link cpython.km:

link.km location target extras

location - where cpython artifacts are. Default: $(dirname ${BASH_SOURCE[0]})/cpython
target   - where to put the results.    Default: $(dirname ${BASH_SOURCE[0]})/cpython
extra    - optional list of files to add (e.g. @f1 @f2...). Default: none
name     - optional km name. Defaul: python.km
EOF
   exit 0
fi

BUILD=$(realpath ${1:-cpython})
OUT=$(realpath ${2:-cpython})
EXTRA_FILES="$3"
NAME=${4:-python.km}

KM_TOP=$(git rev-parse --show-toplevel)
PATH=$(realpath ${KM_TOP}/tools/bin):$PATH

KM_OPT_BIN=/opt/kontain/bin

cd $OUT
kontain-gcc -pthread -ggdb ${BUILD}/Programs/python.o \
   @linkline_km.txt ${EXTRA_FILES} \
   ${BUILD}/libpython3*.a -lz -lssl -lcrypto -lsqlite3 $LDLIBS \
   -o ${NAME} && chmod a-x ${NAME} && echo Linked: ${OUT}/${NAME}

kontain-gcc -dynamic -Xlinker -export-dynamic -pthread -ggdb ${BUILD}/Programs/python.o \
    -Xlinker --undefined=strtoull_l \
   ${BUILD}/libpython3*.a -lcrypto -lsqlite3 $LDLIBS \
   -o ${NAME}d

# Add python->km symlink and make python to looking for libs in correct place
# We want it to work in containers and dev boxes, so locking to /opt/kontain and
# making KM_BLDDIR placement optional
rm -f python ; ln -s ${KM_OPT_BIN}/km python
cat > ${KM_OPT_BIN}/pyvenv.cfg <<- EOF
home = ${OUT}
include-system-site-packages = false
version = ${PYTHON_VERSION}
runtime = kontain
runtime_branch = ${SRC_BRANCH}
runtime_sha = ${SRC+SHA}
EOF
if [ -d "${KM_BLDDIR}" ] ; then
   cp ${KM_OPT_BIN}/pyvenv.cfg ${KM_BLDDIR}
fi
