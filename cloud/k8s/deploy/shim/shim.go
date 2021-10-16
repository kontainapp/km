// +build linux

/*
   Copyright 2021 Kontain Inc
   Copyright The containerd Authors.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

/*
 * This is a containerd shim for KRUN. The idea is a layer on top of the 
 * standard 'runc' that reconfigures it to use the KRUN binary instead of
 * RUNC'. This would be trivially easy with C++ (or Java) class inheritance,
 * but alas GO doesn't do that.
 *
 * The only new logic is in Create where we setup an option to reconfigure
 * BinaryName.
 */

package shim

import (
	"context"
	"os"

	"github.com/containerd/typeurl"
	"github.com/containerd/containerd/runtime/v2/shim"
	taskAPI "github.com/containerd/containerd/runtime/v2/task"
	ptypes "github.com/gogo/protobuf/types"
	runc_opts "github.com/containerd/containerd/runtime/v2/runc/options"
	runc_v2 "github.com/containerd/containerd/runtime/v2/runc/v2"
)

var (
	// check to make sure the *service implements the GRPC API
	_ = (taskAPI.TaskService)(&service{})
)

// New returns a new shim service
func New(ctx context.Context, id string, publisher shim.Publisher, shutdown func()) (shim.Shim, error) {
	p, err := runc_v2.New(ctx, id, publisher, shutdown)
	if err != nil {
		return nil, err
	}
	return &service{
		parent: p,
	}, nil
}

type service struct {
	parent shim.Shim
}

// StartShim is a binary call that executes a new shim returning the address
func (s *service) StartShim(ctx context.Context, opts shim.StartOpts) (string, error) {
	return s.parent.StartShim(ctx, opts)
}

// Cleanup is a binary call that cleans up any resources used by the shim when the service crashes
func (s *service) Cleanup(ctx context.Context) (*taskAPI.DeleteResponse, error) {
	return s.parent.Cleanup(ctx)
}

// Create a new container
func (s *service) Create(ctx context.Context, r *taskAPI.CreateTaskRequest) (_ *taskAPI.CreateTaskResponse, err error) {

	r.Options, err = typeurl.MarshalAny(&runc_opts.Options{
		BinaryName: "/opt/kontain/bin/krun",
	})
	return s.parent.Create(ctx, r)
}

// Start the primary user process inside the container
func (s *service) Start(ctx context.Context, r *taskAPI.StartRequest) (*taskAPI.StartResponse, error) {
	return s.parent.Start(ctx, r)
}

// Delete a process or container
func (s *service) Delete(ctx context.Context, r *taskAPI.DeleteRequest) (*taskAPI.DeleteResponse, error) {
	return s.parent.Delete(ctx, r)
}

// Exec an additional process inside the container
func (s *service) Exec(ctx context.Context, r *taskAPI.ExecProcessRequest) (*ptypes.Empty, error) {
	return s.parent.Exec(ctx, r)
}

// ResizePty of a process
func (s *service) ResizePty(ctx context.Context, r *taskAPI.ResizePtyRequest) (*ptypes.Empty, error) {
	return s.parent.ResizePty(ctx, r)
}

// State returns runtime state of a process
func (s *service) State(ctx context.Context, r *taskAPI.StateRequest) (*taskAPI.StateResponse, error) {
	return s.parent.State(ctx, r)
}

// Pause the container
func (s *service) Pause(ctx context.Context, r *taskAPI.PauseRequest) (*ptypes.Empty, error) {
	return s.parent.Pause(ctx, r)
}

// Resume the container
func (s *service) Resume(ctx context.Context, r *taskAPI.ResumeRequest) (*ptypes.Empty, error) {
	return s.parent.Resume(ctx, r)
}

// Kill a process
func (s *service) Kill(ctx context.Context, r *taskAPI.KillRequest) (*ptypes.Empty, error) {
	return s.parent.Kill(ctx, r)
}

// Pids returns all pids inside the container
func (s *service) Pids(ctx context.Context, r *taskAPI.PidsRequest) (*taskAPI.PidsResponse, error) {
	return s.parent.Pids(ctx, r)
}

// CloseIO of a process
func (s *service) CloseIO(ctx context.Context, r *taskAPI.CloseIORequest) (*ptypes.Empty, error) {
	return s.parent.CloseIO(ctx, r)
}

// Checkpoint the container
func (s *service) Checkpoint(ctx context.Context, r *taskAPI.CheckpointTaskRequest) (*ptypes.Empty, error) {
	return s.parent.Checkpoint(ctx, r)
}

// Connect returns shim information of the underlying service
func (s *service) Connect(ctx context.Context, r *taskAPI.ConnectRequest) (*taskAPI.ConnectResponse, error) {
	return s.parent.Connect(ctx, r)
}

// Shutdown is called after the underlying resources of the shim are cleaned up and the service can be stopped
func (s *service) Shutdown(ctx context.Context, r *taskAPI.ShutdownRequest) (*ptypes.Empty, error) {
	os.Exit(0)
	return &ptypes.Empty{}, nil
}

// Stats returns container level system stats for a container and its processes
func (s *service) Stats(ctx context.Context, r *taskAPI.StatsRequest) (*taskAPI.StatsResponse, error) {
	return s.parent.Stats(ctx, r)
}

// Update the live container
func (s *service) Update(ctx context.Context, r *taskAPI.UpdateTaskRequest) (*ptypes.Empty, error) {
	return s.parent.Update(ctx, r)
}

// Wait for a process to exit
func (s *service) Wait(ctx context.Context, r *taskAPI.WaitRequest) (*taskAPI.WaitResponse, error) {
	return s.parent.Wait(ctx, r)
}
