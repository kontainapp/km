/*
 * Copyright 2021-2022 Kontain Inc
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
 * Command line description:
 *
 * km_cli [-c cmdname] [-p processid] [-d snapshotdir] [-s socket_name] [-l] [-t] [-r]
 *
 * There are 2 parts to this command, selection of processes to snapshot and then
 * snapshotting the selected processes.
 * You can select process by using zero or more command names and or zero or more processes ids.
 * Any commands that match any supplied process id or command name will be added to
 * the list of commands to snapshot.  The -c and -p flags may be specified multiple times.
 * Any matching process that does not have a km associated with it will be ignored.
 * If no command name or process id is supplied, any process using km we be snapshoted.
 * Once processes have been selected each km will be asked to produce a snapshot.
 * The snapshot will be placed in the directory supplied with the -s flag.  If the -s
 * flag is omitted, the snapshots will be placed in the /snapshots directory.
 * The snapshot files will be named with the command's name, the process id, and the suffix .kmsnap.
 * So an example of the snapshot file name for a command named httpd.km would be
 *    /snapshots/httpd.km.1234.kmsnap
 * assuming the default snapshot directory is used.
 *
 * The -s flag is the original interface supplied by this program.  If -s is used the former
 * semantics are used.  The snapshot will be placed in the file named with the km command line's
 * --snapshot=filename flag.
 *
 * The -l flag causes debug logging to stderr to happen.
 * The -t flag causes the km payload to terminate after the snapshot is taken.
 */

/*
 * There are many things to consider when taking snapshots.
 * - we need to snapshot all processes in a container
 * - we need to be able to snapshot only some processes in a container
 * - we need to be able to snapshot a single process that is not in a container, mostly for smoke
 * testing.
 * - we should also tolerate multiple unrelated km instances running outside of a container and not
 * view then as somehow related.
 * - we should probably freeze the processes in a container before snapshoting them.  We don't do
 * this.
 * - we should be able to snapshot with km_cli inside the container and outside the container.
 * - the management pipe used to access a km instance may not be visible from outside the container
 * so running km_cli outsisde a container accessing a mgmt pipe inside a container will fail even
 * though we can discover that pipe's name from outside the container.
 * - if we snapshot processes that are the children of a parent snapshot, the parent-child
 * relationship of these processes is not preserved since the snapshots are restarted seperately.  I
 * think we could solve this problem if we change the snapshot recovery code to be aware that it
 * needs to preserve parent child relationships.  It seems messy but we could do it. We should also
 * consider that the parent processes may remember the pid of its child process and the resumed
 * child process won't have the same pid causing problems when waiting for specific pid to
 * terminate. Snapshoting multiprocess containers is a long way from being supported. These seem to
 * be the requirements for this program.  We don't handle all of them.
 */

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "libkontain_mgmt.h"

char* cmdname;
char* socket_name = NULL;

int debug = 0;
int terminate_app = 1;   // by default the payload is terminated after the snapshot is taken

// Upper limit of -c and -p arguments
#define MAXPIDS 32    // -p limit
#define MAXNAMES 32   // -c limit

// Our value for KM_MGM_LISTEN must match what km is using.
#define KM_MGM_LISTEN 729

// Retry requests to km MAX_RETRIES times
#define MAX_RETRIES 4

#define PROCDIR "/proc"

void usage(void)
{
   fprintf(stderr,
           "Usage: %s [-l] [-c commandname] [-d snapshot_dirname] [-p processid] [-s "
           "socket_name] [-t] [-r]\n",
           cmdname);
   fprintf(stderr, "       -l   = turn on debug logging\n");
   fprintf(stderr,
           "       -c   = search for km processes running commandname (max of %d command names)\n",
           MAXNAMES);
   fprintf(stderr, "       -p   = search for km processes with a process id (max of %d pids)\n", MAXPIDS);
   fprintf(stderr, "       -d   = place snapshots in the specified directory\n");
   fprintf(stderr, "       -s   = use socket_name to request a snapshot\n");
   fprintf(stderr, "       -t   = terminate the km payload after the snapshot completes (default)\n");
   fprintf(stderr, "       -r   = the payload resumes after the snapshot completes\n");
   fprintf(stderr, "       -c and -p flags may be specified multiplte times\n");
}

int send_request(char* sock_name, void* reqp, size_t reqlen)
{
   int sockfd;
   int rc;
   struct sockaddr_un addr = {.sun_family = AF_UNIX};
   if (strlen(sock_name) + 1 > sizeof(addr.sun_path)) {
      fprintf(stderr, "socket name too long\n");
      return E2BIG;
   }
   strcpy(addr.sun_path, sock_name);

   sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (sockfd < 0) {
      rc = errno;
      perror("socket");
      return rc;
   }
   if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      rc = errno;
      perror("connect");
      close(sockfd);
      return rc;
   }
   ssize_t bytes_written = send(sockfd, reqp, reqlen, MSG_NOSIGNAL);
   if (bytes_written < 0) {
      rc = errno;
      perror("send request");
      close(sockfd);
      return rc;
   }
   if (bytes_written != reqlen) {
      fprintf(stderr, "send truncated, wrote %ld bytes, sent %lu bytes\n", bytes_written, reqlen);
      close(sockfd);
      return 1;
   }

   mgmtreply_t reply;
   ssize_t br = recv(sockfd, &reply, sizeof(reply), 0);
   if (br < 0) {
      fprintf(stderr, "snapshot reply not recieved, error %s\n", strerror(errno));
      reply.request_status = errno;
   } else if (br != sizeof(reply)) {
      fprintf(stderr, "reply too small, got %ld bytes, expected %ld bytes\n", br, sizeof(reply));
      reply.request_status = EINVAL;
   }

   close(sockfd);
   return reply.request_status;
}

/*
 * Send a snapshot request on sockname with the supplied parameters.
 * Returns:
 * 0 - success
 * != 0 - failure
 */
int snapshot_process(char* sockname, char* snapshot_file, char* label, char* description, int live)
{
   mgmtrequest_t req;

   req.opcode = KM_MGMT_REQ_SNAPSHOT;
   req.length = sizeof(req.requests.snapshot_req);

   if (label != NULL) {
      strncpy(req.requests.snapshot_req.label, label, sizeof(req.requests.snapshot_req.label) - 1);
   }
   req.requests.snapshot_req.description[0] = 0;
   if (description != NULL) {
      strncpy(req.requests.snapshot_req.description,
              description,
              sizeof(req.requests.snapshot_req.description) - 1);
   }
   req.requests.snapshot_req.live = live;
   req.requests.snapshot_req.snapshot_path[0] = 0;
   if (snapshot_file != NULL) {
      if (strlen(snapshot_file) >= sizeof(req.requests.snapshot_req.snapshot_path)) {
         fprintf(stderr,
                 "snapshot filename %s is too long, %ld bytes allowed\n",
                 snapshot_file,
                 sizeof(req.requests.snapshot_req.snapshot_path));
         return EINVAL;
      }
      strncpy(req.requests.snapshot_req.snapshot_path,
              snapshot_file,
              sizeof(req.requests.snapshot_req.snapshot_path));
   }
   int rc;
   for (int i = 0; i < MAX_RETRIES; i++) {
      if (i != 0) {
         fprintf(stdout, "Retrying snapshot request after transient error\n");
      }
      rc = send_request(sockname, &req, sizeof(req));
      if (rc == 0) {
         break;
      }
      if (rc != EAGAIN) {
         break;
      }
      struct timespec ts = {0, 250000000L};   // .25 seconds
      nanosleep(&ts, NULL);
   }
   return rc;
}

struct found_process {
   char commandname[256];
   int processid;
   char cmdpipename[256];
};
struct found_processes {
   int total_elements;
   int used_elements;
   struct found_process* elements;
};

int processidmatches(pid_t processid, pid_t* processids)
{
   for (int i = 0; processids[i] != 0; i++) {
      if (processid == processids[i]) {
         return 1;
      }
   }
   return 0;
}

int commandnamematches(char* commandname, char* commandnames[])
{
   for (int i = 0; commandnames[i] != NULL; i++) {
      if (strcmp(commandname, commandnames[i]) == 0) {
         if (debug > 0) {
            fprintf(stderr,
                    "commandname %s matches commandnames[%d] %s\n",
                    commandname,
                    i,
                    commandnames[i]);
         }
         return 1;
      }
   }
   return 0;
}

int addprocess(struct found_processes* matched_processes, char* commandname, pid_t processid, char* commandpipe)
{
   if (matched_processes->total_elements <= matched_processes->used_elements) {
      // not enough space, grow.
      int increment = 5;
      struct found_process* new =
          realloc(matched_processes->elements,
                  (matched_processes->total_elements + increment) * sizeof(struct found_process));
      if (new == NULL) {
         return -1;
      }
      matched_processes->elements = new;
      matched_processes->total_elements += increment;
   }
   strncpy(matched_processes->elements[matched_processes->used_elements].commandname,
           commandname,
           sizeof(matched_processes->elements[matched_processes->used_elements].commandname) - 1);
   matched_processes->elements[matched_processes->used_elements].processid = processid;
   strncpy(matched_processes->elements[matched_processes->used_elements].cmdpipename,
           commandpipe,
           sizeof(matched_processes->elements[matched_processes->used_elements].cmdpipename) - 1);
   matched_processes->used_elements++;
   return 0;
}

int readfile(int fd, char* bufp, size_t bufl)
{
   size_t offset = 0;

   while (offset < bufl) {
      ssize_t br = read(fd, &bufp[offset], bufl - offset);
      if (br < 0) {
         fprintf(stderr, "Failed to read file, offset %lu, error %s\n", offset, strerror(errno));
         return -1;
      }
      offset += br;
      if (br == 0) {
         break;
      }
   }
   bufp[offset] = 0;
   return 0;
}

#define PROC_NET_UNIX "/proc/%d/net/unix"
int read_procnetunix(pid_t pid, char* pnu, size_t pnul)
{
   char procpidunix[128];
   snprintf(procpidunix, sizeof(procpidunix), PROC_NET_UNIX, pid);
   int pnufd = open(procpidunix, O_RDONLY);
   if (pnufd >= 0) {
      // Can we read the whole file in one fell swoop?  No.
      int br = readfile(pnufd, pnu, pnul);
      close(pnufd);
      if (br != 0) {
         // read failed
         free(pnu);
         return 1;
      }
   } else {
      return 1;
   }
   return 0;
}

/*
 * Read the fd for the km mgmt pipe.
 * Returns:
 *  0 - no errors encountered
 *  != 0 - something didn't work
 * If there were no errors then check to see if commandpipe was filled with the name of
 * the km mgmt pipe.  We can have no errors but still no pipe.
 * We would like the command to fail on errors but an absent mgmt pipe is not an error.
 */
int get_kmmgmt_pipe(pid_t processid, char* commandpipe, char* pnu, size_t pnul)
{
   char path[512];
   struct stat statb;

   commandpipe[0] = 0;

   // See if there is a km mgmt pipe for this process.
   sprintf(path, "%s/%d/fd/%d", PROCDIR, processid, KM_MGM_LISTEN);
   if (stat(path, &statb) != 0) {
      if (debug > 0) {
         fprintf(stderr,
                 "process %d has %s km mgmt pipe, %s\n",
                 processid,
                 errno == ENOENT ? "no" : "an inaccessible",
                 strerror(errno));
      }
      return 0;
   }
   if (S_ISSOCK(statb.st_mode) == 0) {
      // no mgmt pipe, we cant get a snapshot of this process
      return 0;
   }

   // get the km mgmt fd symlink contents, it should look like "socket:[426720050]"
   // Where that number is an inode number that we can match to a unix pipe name in /proc/XXX/net/unix
   char pipeinode[32];
   ssize_t br = readlink(path, pipeinode, sizeof(pipeinode));
   if (br < 0) {
      fprintf(stderr, "Can't readlink %s, %s\n", path, strerror(errno));
      return errno;
   }
   pipeinode[br] = 0;
   if (debug > 0) {
      fprintf(stderr, "km mgmt fd %d is open on: %s\n", KM_MGM_LISTEN, pipeinode);
   }
   if (strncmp(pipeinode, "socket:[", 8) != 0) {
      // not a km mgmt pipe
      return 0;
   }
   pipeinode[strlen(pipeinode) - 1] = 0;
   if (debug > 0) {
      fprintf(stderr, "search /proc/%d/net/unix for inode %s\n", processid, &pipeinode[8]);
   }

   // now find the entry in /proc/pid/net/unix for the inode number, this gets us the pipe name.
   // We must read this pid's unix file since the process may be in a container and will have a
   // unix file specific to that container.
   if (read_procnetunix(processid, pnu, pnul) != 0) {
      fprintf(stderr, "Unable to read /proc/%d/net/unix file\n", processid);
      return -1;
   }
   if (debug > 0) {
      fprintf(stderr, "/proc/%d/net/unix is %ld bytes long\n", processid, strlen(pnu));
   }
   char* p = strstr(pnu, &pipeinode[8]);
   if (p == NULL) {
      // can't find that inode.  Maybe the socket was closed while we were looking.
      fprintf(stderr, "Didn't find inode %s in /proc/%d/net/unix\n", &pipeinode[8], processid);
      return ENOENT;
   }
   if (debug > 0) {
      fprintf(stderr, "Found net/unix entry: <<<<%.32s>>>>\n", p);
   }

   // Return the pipe file.
   p = strchr(p, ' ') + 1;
   int i;
   for (i = 0; p[i] != '\n'; i++) {
      commandpipe[i] = p[i];
   }
   commandpipe[i] = 0;
   return 0;
}

/*
 * Generate a list of processes that have km and km is listening on the management pipe
 * and matches one of the passed commands or passed process ids.
 * If no command or process id list is provided just take any process(es) listening on the
 * management pipe.
 * Returns a list of matching processes in matched_processes.  The caller must free the
 * returned list.
 * Returned value:
 *   0 - success
 *   != 0 - failure
 */
int find_processes_to_snap(char* commandnames[], pid_t* processids, struct found_processes* matched_processes)
{
   DIR* procdir;
   struct dirent* de;
   struct stat statb;
   int rv = -1;   // expect failure
   size_t pnul =
       200 * 1024;   // for now assume /proc/XXX/net/unix will be no bigger than 200 kilobytes.
   char* pnu = malloc(pnul);
   if (pnu == NULL) {
      fprintf(stderr, "Couldn't allocate %ld byte buffer for /proc/XXX/net/unix file contents\n", pnul);
      return rv;
   }

   // Open /proc to scan the list of processes on the system.
   procdir = opendir(PROCDIR);
   if (procdir == NULL) {
      perror(PROCDIR);
      free(pnu);
      return -1;
   }
   while ((de = readdir(procdir)) != NULL) {
      char path[512];
      if (isdigit(de->d_name[0])) {
         // get the process id
         pid_t processid = atoi(de->d_name);

         // get the command name
         sprintf(path, "%s/%s/cmdline", PROCDIR, de->d_name);
         FILE* cmdfp = fopen(path, "r");
         if (cmdfp == NULL) {
            if (errno == ENOENT) {
               // the process just terminated, no need to look at this one.
               continue;
            }
            fprintf(stderr, "Can't open %s, %s\n", path, strerror(errno));
            break;
         }
         char commandline[256];
         fgets(commandline, sizeof(commandline), cmdfp);
         fclose(cmdfp);
         if (debug > 0) {
            fprintf(stderr, "Checking process id %d, command: %s\n", processid, commandline);
         }

         // Decide if we want this one
         if ((commandnames == NULL && processids == NULL) ||
             (processids != NULL && processidmatches(processid, processids) != 0) ||
             (commandnames != NULL && commandnamematches(commandline, commandnames) != 0)) {
            char commandpipe[256];

            if (get_kmmgmt_pipe(processid, commandpipe, pnu, pnul) != 0) {
               // Some kind of error that is bad enough for us to quit.
               break;
            }
            if (commandpipe[0] == 0) {
               // There is no km mgmt pipe, skip this process.
               continue;
            }

            // The command pipe may only be accessible inside the container.
            // Warn the user but don't quit.
            if (stat(commandpipe, &statb) != 0) {
               fprintf(stderr,
                       "warning: unix socket %s is not accessible, %s\n",
                       commandpipe,
                       strerror(errno));
            }

            if (addprocess(matched_processes, commandline, processid, commandpipe) != 0) {
               fprintf(stderr,
                       "Can't grow process list to %d elements\n",
                       matched_processes->total_elements);
               break;
            }
            if (debug > 0) {
               fprintf(stderr, "pid %d, with command %s matches\n", processid, commandline);
            }
         }
      }
   }
   if (de == NULL) {
      rv = 0;
   }
   if (procdir != NULL) {
      closedir(procdir);
   }
   free(pnu);
   return rv;
}

/*
 * Discover km processes that should be snapshoted based on selection criteria supplied on the
 * command line.  Given that list of processes, send snapshot requests to each process's km.
 */
int main(int argc, char* argv[])
{
   int c;
   int pidindex = 0;
   pid_t commandpids[MAXPIDS + 1];
   int nameindex = 0;
   char* commandnames[MAXNAMES + 1];
   char* snapdir = NULL;

   cmdname = argv[0];

   if (argc < 2) {
      usage();
      return 1;
   }

   while ((c = getopt(argc, argv, "ltrc:d:p:s:")) != -1) {
      switch (c) {
         case 'c':   // snapshot processes with this unix command name
            if (nameindex >= MAXNAMES) {
               fprintf(stderr, "Too many command names specified with -c, %d maximum\n", MAXNAMES);
               exit(1);
            }
            commandnames[nameindex++] = strdup(optarg);
            break;
         case 'd':   // deposit snapshot in this directory in the container
            snapdir = optarg;
            break;
         case 'l':
            debug++;
            break;
         case 'p':   // snapshot the process with this pid.
            if (pidindex >= MAXPIDS) {
               fprintf(stderr, "Too many command pids specified with -p, %d maximum\n", MAXPIDS);
               exit(1);
            }
            commandpids[pidindex++] = atoi(optarg);
            break;
         case 's':
            socket_name = optarg;
            break;
         case 't':
            // The new default is to terminate the payload but keep -t around so scripts don't need
            // to be changed.
            terminate_app = 1;
            break;
         case 'r':
            // Resume payload after snapshot completes
            terminate_app = 0;
            break;
         default:
            fprintf(stderr, "unrecognized option %c\n", c);
            usage();
            return 1;
      }
   }

   commandpids[pidindex] = 0;
   commandnames[nameindex] = NULL;

   // Take a payload snapshot using the km mgmt pipename supplied on the cmd line.
   if (socket_name != NULL) {
      int rc = snapshot_process(socket_name, NULL, NULL, NULL, terminate_app == 0);
      if (rc != 0) {
         fprintf(stderr, "Snapshot via management pipe %s failed, %s\n", socket_name, strerror(rc));
         return 1;
      }

      // if they didn't specify any command names or pids, then we do nothing more.
      if (pidindex == 0 && nameindex == 0) {
         return 0;
      }
   }

   if (snapdir == NULL) {
      snapdir = "/snapshots";
   }

   // Now take snapshots of km process that match the -c and -p command line args
   struct found_processes found_processes = {0, 0, NULL};
   int rc = find_processes_to_snap(commandnames, commandpids, &found_processes);
   if (rc == 0) {
      for (int i = 0; i < found_processes.used_elements; i++) {
         char snapfilename[SNAPPATHMAX];
         char label[SNAPLABELMAX];
         char description[SNAPDESCMAX];
         char* cname = strdup(found_processes.elements[i].commandname);
         snprintf(snapfilename,
                  sizeof(snapfilename),
                  "%s/%s.%d.kmsnap",
                  snapdir,
                  basename(cname),
                  found_processes.elements[i].processid);
         snprintf(label,
                  sizeof(label),
                  "%.120s %d %.120s",
                  cname,
                  found_processes.elements[i].processid,
                  found_processes.elements[i].cmdpipename);
         struct tm* gmt;
         time_t now;
         time(&now);
         gmt = gmtime(&now);
         snprintf(description, sizeof(description), "snapshot date %s", asctime(gmt));
         rc = snapshot_process(found_processes.elements[i].cmdpipename,
                               snapfilename,
                               label,
                               description,
                               terminate_app == 0);
         if (rc != 0) {
            fprintf(stderr,
                    "Cannot create snapshot: cmd %s, pid %d snapshot, %s\n",
                    found_processes.elements[i].commandname,
                    found_processes.elements[i].processid,
                    strerror(rc));
            return 3;
         } else {
            fprintf(stdout,
                    "snapshot for %s:%d is in file %s\n",
                    found_processes.elements[i].commandname,
                    found_processes.elements[i].processid,
                    snapfilename);
         }
         free(cname);
      }
      free(found_processes.elements);
      found_processes.elements = NULL;
      if (found_processes.used_elements == 0) {
         fprintf(stderr, "No matching processes found\n");
         return 2;
      }
   } else {
      return 1;
   }

   return 0;
}
