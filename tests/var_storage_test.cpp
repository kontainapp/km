/*
 * Copyright 2021 Kontain Inc
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
 * Simple check for different storage classes and related linkage for KM payload
 */

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
using namespace std;

#include <stdio.h>
#include <string.h>
std::once_flag flag;

// std::thread::join() has pthread_join as weak. Force the linking
const void* _ = (void*)pthread_join;

void may_throw_function(bool do_throw)
{
   std::stringstream msg;

   // if (do_throw) {
   //    msg << std::this_thread::get_id() << " may_throw_function: throw - expect run_once to
   //    restart\n"; std::cout << msg.str(); throw std::exception();
   // }
   msg << std::this_thread::get_id()
       << "ONCE ONCE ONCE may_throw_function: Didn't throw, call_once will not attempt again\n";   // guaranteed once
   std::cout << msg.str();
}
class StorageType
{
   pthread_t me;
   int var;

   void check_id(void)
   {
      if (me != pthread_self()) {
         cout << "BAD BAD THREAD STORAGE: me=" << std::hex << me << " self=" << std::hex
              << pthread_self() << endl;
      }
   }

 public:
   string name;

   StorageType(const char* _n)
   {
      me = pthread_self();
      name = _n;
      cout << "calling once\n";
      std::call_once(flag, may_throw_function, false);
      pname("Constructor");
   }

   ~StorageType(void) { pname("Destructor"); }

   // void pname(void) { cout << "print name: " + name << endl; }
   void pname(string tag)
   {
      check_id();
      ostringstream id;
      id << "me=0x" << hex << me << " " << tag << " " << name << " thr=0x" << hex << pthread_self()
         << endl;
      cout << id.str();
   }
};

static StorageType t_GlobalStatic("Global static");
StorageType t_Global("Global visible");
static thread_local StorageType t_Tls("tls");

// for thread with entry function
void thread_entry(string n)
{
   string s = "thr " + n + " tls.name=" + t_Tls.name;
   t_Tls.pname(s.c_str());
   // this_thread::sleep_for(chrono::seconds(1));
}

// For thread with callable entry
class check_tls
{
 public:
   void operator()(string n)
   {
      string s = "Thread.operator() tls.name=" + t_Tls.name;
      cout << s << endl;
      t_Tls.pname(s.c_str());
   }
};

double toDouble(const char* buffer, size_t length)
{
   std::istringstream stream(std::string(buffer, length));
   stream.imbue(std::locale("C"));   // Ignore locale
   double d;
   stream >> d;
   return d;
}

int main()
{
   static StorageType t_LocalStatic("local Static");
   StorageType t_main("Local main");

   cout << "======> " << toDouble("1", 1) << endl;

   thread t1(thread_entry, "1");
   thread t2(thread_entry, "2");
   thread t3(check_tls(), "3+");

   t_main.pname("t_main");
   t_LocalStatic.pname("t_LocalStatic");
   t_Global.pname("t_Global");
   t_GlobalStatic.pname("t_GlobalStatic");

   t1.join();
   t3.join();
   t2.join();

   cout << "after join" << endl;
   cout.flush();

   return 0;
}
