package image

import (
	"encoding/json"
	"os"
	"path/filepath"

	"github.com/opencontainers/go-digest"
	"github.com/pkg/errors"
)

// func ExportGetMetadata(path string) (*Image, error) {
// 	return nil, nil
// }

const (
	manifestFileName           = "manifest.json"
	legacyLayerFileName        = "layer.tar"
	legacyConfigFileName       = "json"
	legacyVersionFileName      = "VERSION"
	legacyRepositoriesFileName = "repositories"
)

type manifest struct {
	Config   string
	RepoTags []string
	Layers   []string
	Parent   digest.Digest `json:",omitempty"`
}

type ExportedImage struct {
}

func some(path string) error {

	manifestFile, err := os.Open(filepath.Join(path, manifestFileName))
	if err != nil {
		return err
	}

	defer manifestFile.Close()

	var out []manifest
	if err := json.NewDecoder(manifestFile).Decode(&out); err != nil {
		return err
	}

	// We should only export a single image.
	if len(out) != 1 {
		return errors.New("Failed. We only export one image layer")
	}

	// manifest := out[0]

	return nil
}
