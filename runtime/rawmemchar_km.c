#include <string.h>

void *__rawmemchr (const void *s, int c)
{
  if (c != '\0') {
    return memchr (s, c, (size_t)-1);
  }
  return (char *)s + strlen (s);
}

weak_alias(__rawmemchr, rawmemchr);