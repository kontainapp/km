/*
 * Copyright © 2019 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 *
 * Basic test for C++ throw/catch and related libgcc/unwind.
 */

#include <iostream>

using namespace std;

int main()
{
   string f("throw test: ");
   // Some code
   cout << f << "Before exception\n";
   try {
      cerr << f << "Ready to throw\n";
      throw "Crash !";
      cout << f << "After throw (Never executed)\n";
   } catch (...) {
      cout << f << "Exception Caught !\n";
   }
}