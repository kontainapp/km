
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "greatest/greatest.h"

TEST test_huggingface()
{
  struct addrinfo *res;
  int rc = getaddrinfo("huggingface.co", "443", NULL, &res);

  if (rc != 0) {
    fprintf(stderr, "getaddinfo returned error %d(%s)\n", rc, gai_strerror(rc));
    ASSERT(0);
  }
  PASS();
}

GREATEST_MAIN_DEFS();

int
main(int argc, char *argv[])
{
   GREATEST_MAIN_BEGIN();
   RUN_TEST(test_huggingface);
   GREATEST_PRINT_REPORT();
   return greatest_info.failed;
}
