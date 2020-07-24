package image

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"github.com/docker/distribution"
	"github.com/opencontainers/go-digest"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

const (
	manifestFileName           = "manifest.json"
	legacyLayerFileName        = "layer.tar"
	legacyConfigFileName       = "json"
	legacyVersionFileName      = "VERSION"
	legacyRepositoriesFileName = "repositories"
)

type manifest struct {
	Config       string
	RepoTags     []string
	Layers       []string
	Parent       digest.Digest                      `json:",omitempty"`
	LayerSources map[DiffID]distribution.Descriptor `json:",omitempty"`
}

type ExportedImage struct {
	path     string
	manifest *manifest
	config   *Image
}

func (img *ExportedImage) Commit() error {
	newConfigData, err := json.Marshal(img.config)
	if err != nil {
		return errors.Wrap(err, "Failed to marshal configs")
	}

	newID := digest.FromBytes(newConfigData).Hex()
	newConfigFilename := fmt.Sprintf("%s.json", newID)
	newConfigPath := filepath.Join(img.path, newConfigFilename)

	logrus.WithFields(logrus.Fields{
		"new id":          newID,
		"new config name": newConfigFilename,
		"new config path": newConfigPath,
	}).Debug("Commit: compute a new ID")
	if err := ioutil.WriteFile(newConfigPath, newConfigData, 0644); err != nil {
		return errors.Wrap(err, "Failed to write out the new config")
	}

	// Write out the new config filename in manifest and write the manifest
	newManifest := *(img.manifest)
	newManifest.Config = newConfigFilename
	newManifestData, err := json.Marshal([]manifest{newManifest})
	if err != nil {
		return errors.Wrap(err, "Failed to marshal the new manifest")
	}

	manifestPath := filepath.Join(img.path, manifestFileName)
	if err := ioutil.WriteFile(manifestPath, newManifestData, 0644); err != nil {
		return errors.Wrap(err, "Failed to rewrite the manifest")
	}

	return nil
}

func (img *ExportedImage) UpdateName(name string) {
	img.manifest.RepoTags = []string{name}
}

func loadManifest(path string) (*manifest, error) {
	logrus.WithFields(logrus.Fields{
		"path": path,
	}).Debug("load manifest")
	manifestPath := filepath.Join(path, manifestFileName)
	manifestFile, err := os.Open(manifestPath)
	if err != nil {
		return nil, err
	}

	defer manifestFile.Close()

	var out []manifest
	if err := json.NewDecoder(manifestFile).Decode(&out); err != nil {
		return nil, err
	}

	// We should only export a single image.
	if len(out) != 1 {
		return nil, errors.New("Failed. We only export one image layer")
	}

	manifest := out[0]
	return &manifest, nil
}

func loadConfig(path string) (*Image, error) {
	logrus.WithFields(logrus.Fields{
		"path": path,
	}).Debug("load config")
	configFile, err := os.Open(path)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to open config file")
	}

	defer configFile.Close()

	var out Image
	if err := json.NewDecoder(configFile).Decode(&out); err != nil {
		return nil, errors.Wrap(err, "Failed to decode the config")
	}

	return &out, nil
}

func LoadExportedImage(path string) (*ExportedImage, error) {
	logrus.WithFields(logrus.Fields{
		"path": path,
	}).Debug("LoadExportedImage")
	manifest, err := loadManifest(path)
	if err != nil {
		return nil, err
	}

	configPath := filepath.Join(path, manifest.Config)
	config, err := loadConfig(configPath)
	if err != nil {
		return nil, err
	}

	return &ExportedImage{
		path:     path,
		manifest: manifest,
		config:   config,
	}, nil
}
