/*
 * Copyright 2021 Kontain Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
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
