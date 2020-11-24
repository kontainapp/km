#!/bin/bash
# -fcommon gcc flag is needed for gcc10, see https://bugs.openjdk.java.net/browse/JDK-8235903
cd ${JDK_VERSION} && bash configure \
    --disable-warnings-as-errors --with-native-debug-symbols=none \
    --with-extra-cflags='-fcommon'  \
    --with-jvm-variants=server --with-zlib=bundled --with-jtreg=$(realpath jtreg) \
    --enable-jtreg-failure-handler
make -C ${JDK_VERSION} images
# build HotSpot disassember plugin. Useful for failure analysis.
curl --output ${BLDDIR}/binutils-2.19.1.tar.bz2 https://ftp.gnu.org/gnu/binutils/binutils-2.19.1.tar.bz2
bunzip2 ${BLDDIR}/binutils-2.19.1.tar.bz2
(cd ${BLDDIR} ; tar --overwrite -xf binutils-2.19.1.tar)
make -C ${JDK_VERSION}/src/utils/hsdis BINUTILS=${BLDDIR}/binutils-2.19.1 CFLAGS="-Wno-error -fPIC" all64
cp ${JDK_VERSION}/src/utils/hsdis/build/linux-amd64/hsdis-amd64.so ${JAVA_DIR}/lib/hsdis-amd64.so
./link-km.sh ${JDK_BUILD_DIR}