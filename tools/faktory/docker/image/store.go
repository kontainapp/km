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
