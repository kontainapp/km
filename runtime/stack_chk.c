/*
 * Copyright © 2018 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Per refspecs
 * http://refspecs.linux-foundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/libc---stack-chk-fail-1.html
 *
 * "The interface __stack_chk_fail() shall abort the function that called it
 * with a message that a stack overflow has been detected. The program that
 * called the function shall then exit. The interface __stack_chk_fail() does
 * not check for a stack overflow itself. It merely reports one when invoked."
 */

#include <stdio.h>

void __stack_chk_fail(void)
{
   puts("stack overflow detected");
}
