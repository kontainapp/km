// Copyright 2021 Kontain Inc
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


package image

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"sync"

	digest "github.com/opencontainers/go-digest"
	"github.com/pkg/errors"
)

const (
	contentDirName  = "content"
	metadataDirName = "metadata"
)

// Store is the filesystem access layer for the image info on the disk
type Store struct {
	sync.RWMutex
	root string
}

// NewStore ...
func NewStore(root string) (*Store, error) {
	s := &Store{
		root: root,
	}

	if err := os.MkdirAll(filepath.Join(root, contentDirName, string(digest.Canonical)), 0700); err != nil {
		return nil, errors.Wrap(err, "failed to create storage backend")
	}
	if err := os.MkdirAll(filepath.Join(root, metadataDirName, string(digest.Canonical)), 0700); err != nil {
		return nil, errors.Wrap(err, "failed to create storage backend")
	}

	return s, nil
}

func (s *Store) contentFile(dgst digest.Digest) string {
	return filepath.Join(s.root, contentDirName, string(dgst.Algorithm()), dgst.Hex())
}

func (s *Store) metadataDir(dgst digest.Digest) string {
	return filepath.Join(s.root, metadataDirName, string(dgst.Algorithm()), dgst.Hex())
}

// Get will read the metadata from docker internal store.
func (s *Store) Get(id string) (*Image, error) {
	s.RLock()
	defer s.RUnlock()

	dgst := digest.NewDigestFromHex(digest.SHA256.String(), id)
	content, err := ioutil.ReadFile(s.contentFile(dgst))
	if err != nil {
		return nil, errors.Wrapf(err, "failed to get digest %s", dgst)
	}

	image, err := NewFromJSON(content)
	if err != nil {
		return nil, errors.Wrap(err, "failed to create image from content")
	}

	return image, nil
}
