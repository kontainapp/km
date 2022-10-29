/*
 * A test to exercise pre-snapshot recovery listen feature.
 * First setup to listen for a connection on some port.
 * Then block in one of epoll_wait/select/poll waiting for a new
 * connection.
 * Then initiate generation of a km snapshot.
 * Terminate that instance of the test program.
 * Then set the SNAP_LISTEN_PORT environment variable to the port number the
 * test will be listening on when the snapshot is recovered.
 * Recover the snapshot.  It should be blcoked in whatever poll etc..
 * Next initiate a connection attempt to the test program.
 * poll or whatever should fire.
 * Verify that it fired.
 * Then do the same for select and epoll_wait.
 * Then actually accept the connection.
 * The test is done.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

void usage(void)
{
   fprintf(stderr, "prelisten port#\n");
}

int main(int argc, char* argv[])
{
   if (argc < 2) {
      usage();
      return 1;
   }

   int port = atoi(argv[1]);
   fprintf(stdout, "Using port %d\n", port);

   int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (sockfd < 0) {
      fprintf(stderr, "socket() failed, %s\n", strerror(errno));
      return 1;
   }

   struct sockaddr_in sa = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY};
   sa.sin_port = htons(port);
   if (bind(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
      fprintf(stderr, "bind() to port %d failed, %s\n", port, strerror(errno));
      return 1;
   }

   if (listen(sockfd, 1) != 0) {
      fprintf(stderr, "listen() on port %d failed, %s\n", port, strerror(errno));
      return 1;
   }

   fprintf(stdout, "Blocking to wait for new connection on port %d\n", port);

   // Verify select()
   fd_set readfds;
   fd_set writefds;
   fd_set exceptfds;
   FD_ZERO(&readfds);
   FD_ZERO(&writefds);
   FD_ZERO(&exceptfds);
   FD_SET(sockfd, &readfds);
   int nfd = select(sockfd + 1, &readfds, &writefds, &exceptfds, NULL);
   if (nfd < 0) {
      fprintf(stderr, "select() failed, %s\n", strerror(errno));
      return 1;
   }
   if (nfd != 1) {
      fprintf(stderr, "expected select to return 1, actually returned %d\n", nfd);
      return 1;
   }
   // Verify that only sockfd is set in readfds
   int failcount = 0;
   for (int i = 0; i < FD_SETSIZE; i++) {
      if (FD_ISSET(i, &writefds)) {
         fprintf(stderr, "fd %d is set in writefds and should not be\n", i);
         failcount++;
      }
      if (FD_ISSET(i, &exceptfds)) {
         fprintf(stderr, "fd %d is set in exceptfds and should not be\n", i);
         failcount++;
      }
      if (i == sockfd) {
         if (FD_ISSET(i, &readfds)) {
            fprintf(stderr, "sockfd %d is set in readfds as expected\n", sockfd);
         } else {
            fprintf(stderr, "sockfd %d is not set in readfds and should be\n", sockfd);
            failcount++;
         }
      }
   }
   fprintf(stdout, "select() returned that input is available on listening fd %d\n", sockfd);

   // Verify poll()
   struct pollfd p = {.fd = sockfd, .events = POLLIN, .revents = 0};
   int pollfds = poll(&p, 1, 0);
   if (pollfds < 0) {
      fprintf(stderr, "poll() failed, %s\n", strerror(errno));
      return 1;
   }
   if (p.revents != POLLIN) {
      fprintf(stderr, "unexpected contents of revents, got 0x%x, expected 0x%x\n", p.revents, POLLIN);
      return 1;
   }
   fprintf(stdout, "poll() returned that input is available on listening fd %d\n", sockfd);

   // verify epoll()
   int epfd = epoll_create(1);
   if (epfd < 0) {
      fprintf(stderr, "epoll_create() failed, %s\n", strerror(errno));
      return 1;
   }
   struct epoll_event event = {.events = EPOLLIN, .data.u64 = 22334499};
   int rc = epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);
   if (rc < 0) {
      fprintf(stderr, "epoll_ctl() failed, %s\n", strerror(errno));
      return 1;
   }
   struct epoll_event gotthisback;
   int epoll_event_count = epoll_wait(epfd, &gotthisback, 1, -1);
   if (epoll_event_count < 0) {
      fprintf(stderr, "epoll_wait() failed, %s\n", strerror(errno));
      return 1;
   }
   if (epoll_event_count != 1) {
      fprintf(stderr, "epoll_wait() returned %d, expected it to return 1\n", epoll_event_count);
      return 1;
   }
   if (gotthisback.events != EPOLLIN) {
      fprintf(stderr,
              "epoll_wait() returned events 0x%x, expected it to return 0x%x\n",
              gotthisback.events,
              EPOLLIN);
      return 1;
   }
   fprintf(stdout, "epoll_wait() returned that input is available on listening fd %d\n", sockfd);
   fprintf(stdout, "gotthisback.data.u64 contains %ld\n", gotthisback.data.u64);
   close(epfd);

   // Do the accept() to see if we get the fd.
   struct sockaddr acceptedaddr;
   socklen_t addrlen;
   int acceptedfd = accept(sockfd, &acceptedaddr, &addrlen);
   if (acceptedfd < 0) {
      fprintf(stderr, "accept() failed, %s\n", strerror(errno));
      return 1;
   }
   fprintf(stderr, "accept returned fd %d\n", acceptedfd);

   close(acceptedfd);
   close(sockfd);

   return 0;
}
