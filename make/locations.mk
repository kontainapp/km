# Copyright © 2018 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
# A helper include. ALlowes to include some vars in dirs which cannot include
# actions.mk (e.g. tests which need their own compike /link flags)

# this is the path from the TOP to current dir
FROMTOP := $(shell git rev-parse --show-prefix)
# all build results (including obj etc..)  go under this one
BLDTOP := ${TOP}build/
# Build results go here,
# For different build types (e.g. coverage), pass BLDTYPE=<type>/, e.g BLDTYPE=coverage/ (with trailing /)
BLDDIR := ${BLDTOP}${FROMTOP}$(BLDTYPE)

# km location needs to be fixed no matter what is the FROMTOP,
# so we can use KM from different places
KM_BLDDIR := ${BLDTOP}km/$(BLDTYPE)
KM_BIN := ${KM_BLDDIR}km

# Code coverage support. Assumes 'lcov' is installed.
# See man 1 geninifo for source code markers which control coverage generating, if needed
COV_BLDTYPE := coverage

# tracefile and final report locations
COV_INFO    = $(BLDDIR)coverage.info
# location for html report
COV_REPORT  = $(BLDDIR)lcov-report

# Generic support - applies for all flavors (SUBDIR, EXEC, LIB, whatever)

# Support for "make help"
#
# colors for nice color in output
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m
