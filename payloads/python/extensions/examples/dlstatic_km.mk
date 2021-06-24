#
# GENERATED FILE. DO NOT EDIT.
#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# The makefile does the following:
#   uses objcopy to preprocess .o files (replace symbols) based on base.km.symmap
#   compiles (-c) base.km.symbols.c file
#   builds .a for each .so
#   links the result by passing .o files explicitly and .a files as -l

KM_RUNTIME_INCLUDES := /home/$(USER)/workspace/km/runtime
CFLAGS := -g -I$(KM_RUNTIME_INCLUDES)
LINK_LINE_FILE := linkline_km.txt
KM_LIB_EXT := .km.lib.a

LIBS := \
	build/lib.linux-x86_64-3.7/numpy/core/_dummy.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_multiarray_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_multiarray_umath.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_umath_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_rational_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_struct_ufunc_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/core/_operand_flag_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/fft/_pocketfft_internal.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/linalg/lapack_lite.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/linalg/_umath_linalg.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/mt19937.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/philox.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/pcg64.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/sfc64.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/common.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/bit_generator.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/generator.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/bounded_integers.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} \
	build/lib.linux-x86_64-3.7/numpy/random/mtrand.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT}

SYMOBJ := $(subst ${KM_LIB_EXT},.km.symbols.o,$(LIBS))

# Build libararies and create text file with .o and .a for linker
all: $(SYMOBJ) $(LIBS)
	@rm -f ${LINK_LINE_FILE}
	@for i in ${SYMOBJ} ${LIBS} ; \
   do \
     echo $$(realpath $$i) >> ${LINK_LINE_FILE} ; \
   done && echo Saved link line to ${CURDIR}/${LINK_LINE_FILE}

clean:
	@items=$$(find . -name '*km.*[oa]') ; if [[ ! -z $$items ]] ; then rm -v $$items ; fi



build/lib.linux-x86_64-3.7/numpy/core/_dummy.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/core/src/dummymodule.o
		@id=_3181ac773cc6b3ecaaf5ac0bfd4d5063_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_dummy.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_multiarray_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/_multiarray_tests.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/mem_overlap.o
		@id=_1598e811201ddfcbc4bc75ecce3ce21e_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_multiarray_tests.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_multiarray_umath.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/alloc.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/arrayobject.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/arraytypes.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/array_assign_scalar.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/array_assign_array.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/arrayfunction_override.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/buffer.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/calculation.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/compiled_base.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/common.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/convert.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/convert_datatype.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/conversion_utils.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/ctors.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/datetime.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/datetime_strings.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/datetime_busday.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/datetime_busdaycal.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/descriptor.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/dragon4.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/dtype_transfer.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/einsum.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/flagsobject.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/getset.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/hashdescr.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/item_selection.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/iterators.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/lowlevel_strided_loops.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/mapping.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/methods.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/multiarraymodule.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/nditer_templ.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/nditer_api.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/nditer_constr.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/nditer_pywrap.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/number.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/refcount.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/sequence.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/shape.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/scalarapi.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/multiarray/scalartypes.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/strfuncs.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/temp_elide.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/typeinfo.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/usertypes.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/multiarray/vdot.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/umathmodule.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/reduction.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/loops.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/matmul.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/clip.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/ufunc_object.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/extobj.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/cpuid.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/scalarmath.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/ufunc_type_resolution.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/umath/override.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/npymath/npy_math.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/npymath/ieee754.o \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/npymath/npy_math_complex.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/npymath/halffloat.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/array_assign.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/mem_overlap.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/npy_longdouble.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/ucsnarrow.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/ufunc_override.o \
	build/temp.linux-x86_64-3.7/numpy/core/src/common/numpyos.o
		@id=_9839822d40165c6c7c4f5d130a1e21b8_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_multiarray_umath.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_umath_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/_umath_tests.o
		@id=_209e8102f3cc7a460b0e3a055512f943_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_umath_tests.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_rational_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/_rational_tests.o
		@id=_ab1e2522dd558d82af92533654f0a361_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_rational_tests.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_struct_ufunc_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/_struct_ufunc_tests.o
		@id=_a9582af97a1f41b9713e5bafd7f4dd5a_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_struct_ufunc_tests.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/core/_operand_flag_tests.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/core/src/umath/_operand_flag_tests.o
		@id=_4d5804fabac89b4ece3aae51d872cdac_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/core/_operand_flag_tests.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/fft/_pocketfft_internal.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/fft/_pocketfft.o
		@id=_bae671b650110294dff65f53ec52c997_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/fft/_pocketfft_internal.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/linalg/lapack_lite.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_litemodule.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/python_xerbla.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_z_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_c_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_d_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_s_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_blas.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_config.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c.o
		@id=_d27311cb0d662fde6cc225f087c4808a_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/linalg/lapack_lite.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/linalg/_umath_linalg.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/build/src.linux-x86_64-3.7/numpy/linalg/umath_linalg.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/python_xerbla.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_z_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_c_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_d_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_s_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_lapack.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_blas.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c_config.o \
	build/temp.linux-x86_64-3.7/numpy/linalg/lapack_lite/f2c.o
		@id=_a4dea1cb40b5b1d2204fa65500d11800_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/linalg/_umath_linalg.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/mt19937.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/mt19937.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/mt19937/mt19937.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/mt19937/mt19937-jump.o
		@id=_a758b2765dbc1aa920150916da9be3d1_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/mt19937.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/philox.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/philox.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/philox/philox.o
		@id=_a40983cfb4b897999b26def15b22623b_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/philox.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/pcg64.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/pcg64.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/pcg64/pcg64.o
		@id=_dbe020164909d7144527ada0e36334ab_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/pcg64.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/sfc64.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/sfc64.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/sfc64/sfc64.o
		@id=_f3d6f06db41b99cad2e839573ec7cfec_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/sfc64.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/common.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/common.o
		@id=_7fe6a310239ae68545255ff8a785db6f_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/common.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/bit_generator.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/bit_generator.o
		@id=_d23b2ec039d29302d937e7b1acf65774_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/bit_generator.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/generator.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/generator.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/logfactorial.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/distributions.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/random_hypergeometric.o
		@id=_8a0ef3b0b70700f40d4425e7df11d20f_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/generator.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/bounded_integers.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/bounded_integers.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/logfactorial.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/distributions.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/random_hypergeometric.o
		@id=_3c6471cb88b208628fa62af0240bd449_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/bounded_integers.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles

build/lib.linux-x86_64-3.7/numpy/random/mtrand.cpython-37m-x86_64-linux-gnu${KM_LIB_EXT} : \
	build/temp.linux-x86_64-3.7/numpy/random/mtrand.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/legacy/legacy-distributions.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/logfactorial.o \
	build/temp.linux-x86_64-3.7/numpy/random/src/distributions/distributions.o
		@id=_0db20fdfe5524a1d66a55d635a47bc2e_km ; echo Library=$@ id=$$id; \
		for obj in $^ ; do \
			munged_obj=$${obj/.o/$$id.o} ; cp $$obj $$munged_obj; ofiles="$$ofiles $$munged_obj" ; \
			objcopy --redefine-syms=build/lib.linux-x86_64-3.7/numpy/random/mtrand.cpython-37m-x86_64-linux-gnu.km.symmap $$munged_obj; \
		done && ar rv $@ $$ofiles


# allows to do 'make print-varname'
print-%  : ; @echo $* = $($*)