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
