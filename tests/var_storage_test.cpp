
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
 * Simple check for different storage classes and related linkage for KM payload
 */

// TODO add __thread_local.

#include <iostream>
#include <string>
using namespace std;

#include <stdio.h>

__attribute__((__weak__)) void* __dso_handle = (void*)&__dso_handle;

class StorageType
{
   string name;
   int var;
   char large_buffer[4096];

 public:
   StorageType(const char* _n)
   {
      printf("Constructor %s\n", _n);
      name = _n;
      cout << "\tiostream cout " << name << endl;
   }
   ~StorageType(void)
   {
      printf("Destructor %s\n", name.c_str());
      cout << "\tiostream cout " << name << endl;
   }

   // void pname(void) { cout << "print name: " + name << endl; }
   void pname(const char* const tag) { printf("print name:%s: %s\n", tag, name.c_str()); }
};

static StorageType t_GlobalStatic("Global static");

StorageType t_Global("Global visible");

int main()
{
   StorageType t_main("Local main");
   static StorageType t_LocalStatic("local Static");

   t_main.pname("t_main");
   t_LocalStatic.pname("t_LocalStatic");
   t_Global.pname("t_Global");
   t_GlobalStatic.pname("t_GlobalStatic");
   return 0;
}