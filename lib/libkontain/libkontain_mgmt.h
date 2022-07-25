#ifndef __LIBKONTAIN_MGMT_H__
#define __LIBKONTAIN_MGMT_H__

typedef enum km_mgmt_request {
   KM_MGMT_REQ_SNAPSHOT			// request a payload snapshot
} km_mgmt_request_t;

/*
 * Send this structure in the unix socket to the km management thread
 * when trying to create a payload snapshot.
 */
#define SNAPLABELMAX 256
#define SNAPDESCMAX 256
#define SNAPPATHMAX 1024
typedef struct mgmtrequest {
   km_mgmt_request_t opcode;		// what mgmt request is this.
   int length;				// length of this request including opcode and this length
   union {
      struct snapshot_req {
         char label[SNAPLABELMAX];	// a label placed into the snapshot (coredump)
         char description[SNAPDESCMAX];	// a description placed in the snapshot (coredump)
         int live;			// if non-zero, the payload keeps running after the snapshot, if zero payload terminates
         char snapshot_path[SNAPPATHMAX];// path to where the snapshot should be placed.
      } snapshot_req;
   } requests;
} mgmtrequest_t;

typedef struct mgmtreply {
   int request_status;			// 0 = success, non-zero is a unix errno
   // for requests that return information, add structure definitions here
} mgmtreply_t;

#endif // !defined(__LIBKONTAIN_MGMT_H__)
