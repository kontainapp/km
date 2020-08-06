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

import digest "github.com/opencontainers/go-digest"

// DiffID is the hash of an individual layer tar.
type DiffID digest.Digest

// RootFS describes images root filesystem
// This is currently a placeholder that only supports layers. In the future
// this can be made into an interface that supports different implementations.
type RootFS struct {
	Type    string   `json:"type"`
	DiffIDs []DiffID `json:"diff_ids,omitempty"`
}
