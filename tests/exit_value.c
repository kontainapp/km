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
 * Test passing value from exit() to KM
 */

#include <stdlib.h>

int main()
{
   // return magic '17' to validate it's passing all the way up
   // We'll test for '17' upstairs in tests
   exit(17);
}
