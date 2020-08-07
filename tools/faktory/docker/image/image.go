// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package image

import (
	"encoding/json"
	"errors"
	"runtime"
	"time"

	"github.com/docker/docker/api/types/container"
	digest "github.com/opencontainers/go-digest"
)

// ID is the content-addressable ID of an image.
type ID digest.Digest

func (id ID) String() string {
	return id.Digest().String()
}

// Digest converts ID into a digest
func (id ID) Digest() digest.Digest {
	return digest.Digest(id)
}

// IDFromDigest creates an ID from a digest
func IDFromDigest(digest digest.Digest) ID {
	return ID(digest)
}

// V1Image stores the V1 image configuration.
type V1Image struct {
	// ID is a unique 64 character identifier of the image
	ID string `json:"id,omitempty"`
	// Parent is the ID of the parent image
	Parent string `json:"parent,omitempty"`
	// Comment is the commit message that was set when committing the image
	Comment string `json:"comment,omitempty"`
	// Created is the timestamp at which the image was created
	Created time.Time `json:"created"`
	// Container is the id of the container used to commit
	Container string `json:"container,omitempty"`
	// ContainerConfig is the configuration of the container that is committed into the image
	ContainerConfig container.Config `json:"container_config,omitempty"`
	// DockerVersion specifies the version of Docker that was used to build the image
	DockerVersion string `json:"docker_version,omitempty"`
	// Author is the name of the author that was specified when committing the image
	Author string `json:"author,omitempty"`
	// Config is the configuration of the container received from the client
	Config *container.Config `json:"config,omitempty"`
	// Architecture is the hardware that the image is built and runs on
	Architecture string `json:"architecture,omitempty"`
	// Variant is the CPU architecture variant (presently ARM-only)
	Variant string `json:"variant,omitempty"`
	// OS is the operating system used to build and run the image
	OS string `json:"os,omitempty"`
	// Size is the total size of the image including all layers it is composed of
	Size int64 `json:",omitempty"`
}

// Image stores the image configuration
type Image struct {
	V1Image
	Parent     ID        `json:"parent,omitempty"` //nolint:govet
	RootFS     *RootFS   `json:"rootfs,omitempty"`
	History    []History `json:"history,omitempty"`
	OSVersion  string    `json:"os.version,omitempty"`
	OSFeatures []string  `json:"os.features,omitempty"`

	// rawJSON caches the immutable JSON associated with this image.
	rawJSON []byte

	// computedID is the ID computed from the hash of the image config.
	// Not to be confused with the legacy V1 ID in V1Image.
	computedID ID
}

// RawJSON returns the immutable JSON associated with the image.
func (img *Image) RawJSON() []byte {
	return img.rawJSON
}

// ID returns the image's content-addressable ID.
func (img *Image) ID() ID {
	return img.computedID
}

// ImageID stringifies ID.
func (img *Image) ImageID() string {
	return img.ID().String()
}

// RunConfig returns the image's container config.
func (img *Image) RunConfig() *container.Config {
	return img.Config
}

// BaseImgArch returns the image's architecture. If not populated, defaults to the host runtime arch.
func (img *Image) BaseImgArch() string {
	arch := img.Architecture
	if arch == "" {
		arch = runtime.GOARCH
	}
	return arch
}

// BaseImgVariant returns the image's variant, whether populated or not.
// This avoids creating an inconsistency where the stored image variant
// is "greater than" (i.e. v8 vs v6) the actual image variant.
func (img *Image) BaseImgVariant() string {
	return img.Variant
}

// OperatingSystem returns the image's operating system. If not populated, defaults to the host runtime OS.
func (img *Image) OperatingSystem() string {
	os := img.OS
	if os == "" {
		os = runtime.GOOS
	}
	return os
}

// MarshalJSON serializes the image to JSON. It sorts the top-level keys so
// that JSON that's been manipulated by a push/pull cycle with a legacy
// registry won't end up with a different key order.
func (img *Image) MarshalJSON() ([]byte, error) {
	type MarshalImage Image

	pass1, err := json.Marshal(MarshalImage(*img))
	if err != nil {
		return nil, err
	}

	var c map[string]*json.RawMessage
	if err := json.Unmarshal(pass1, &c); err != nil {
		return nil, err
	}
	return json.Marshal(c)
}

// History stores build commands that were used to create an image
type History struct {
	// Created is the timestamp at which the image was created
	Created time.Time `json:"created"`
	// Author is the name of the author that was specified when committing the image
	Author string `json:"author,omitempty"`
	// CreatedBy keeps the Dockerfile command used while building the image
	CreatedBy string `json:"created_by,omitempty"`
	// Comment is the commit message that was set when committing the image
	Comment string `json:"comment,omitempty"`
	// EmptyLayer is set to true if this history item did not generate a
	// layer. Otherwise, the history item is associated with the next
	// layer in the RootFS section.
	EmptyLayer bool `json:"empty_layer,omitempty"`
}

// NewHistory creates a new history struct from arguments, and sets the created
// time to the current time in UTC
func NewHistory(author, comment, createdBy string, isEmptyLayer bool) History {
	return History{
		Author:     author,
		Created:    time.Now().UTC(),
		CreatedBy:  createdBy,
		Comment:    comment,
		EmptyLayer: isEmptyLayer,
	}
}

// NewFromJSON creates an Image configuration from json.
func NewFromJSON(src []byte) (*Image, error) {
	img := &Image{}

	if err := json.Unmarshal(src, img); err != nil {
		return nil, err
	}
	if img.RootFS == nil {
		return nil, errors.New("invalid image JSON, no RootFS key")
	}

	img.rawJSON = src

	return img, nil
}
