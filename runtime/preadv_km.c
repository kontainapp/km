#include <unistd.h>
#include <sys/uio.h>
#include "syscall.h"

ssize_t preadv(int fd, const struct iovec* iov, int count, off_t ofs)
{
   return syscall_cp(SYS_preadv, fd, iov, count, (long)(ofs), (long)(ofs >> 32));
}

weak_alias(preadv, preadv64);
weak_alias(preadv, preadv64v2);
