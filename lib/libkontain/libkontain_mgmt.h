#ifndef __LIBKONTAIN_MGMT_H__
#define __LIBKONTAIN_MGMT_H__

#define KM_MGMT_REQ_SNAPSHOT	1234

/*
 * Send this structure in the unix socket to the km management thread
 * when trying to create a payload snapshot.
 */
#define SNAPLABELMAX 256
#define SNAPDESCMAX 256
#define SNAPPATHMAX 1024
struct snapshot_req {
   int opcode;				// what mgmt request is this.
   int length;				// length of this request including opcode and the length
   char label[SNAPLABELMAX];		// a label placed into the snapshot (coredump)
   char description[SNAPDESCMAX];	// a description placed in the snapshot (coredump)
   int live;				// if non-zero, the payload keeps running after the snapshot, if zero payload terminates
   char snapshot_path[SNAPPATHMAX];	// path to where the snapshot should be placed.
};

struct mgmtreply {
   int request_status;			// 0 = success, non-zero is a unix errno
};

#endif // !defined(__LIBKONTAIN_MGMT_H__)
