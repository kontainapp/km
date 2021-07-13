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

// ExportedImage contains unmarshaled information read from unpacked image from
// `docker save`.
type ExportedImage struct {
	path     string
	manifest *manifest
	config   *Image
}

// Commit will write the existing data back. We choose to write in place here,
// so the old metadata is overwritten.
func (img *ExportedImage) Commit() error {

	logrus.WithFields(logrus.Fields{
		"workdir": img.config.Config.WorkingDir,
	}).Debug("Commit Images")

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

// UpdateName updates the name of the image. `docker load` will only read the
// image tag name from the manifest, not as an arguement.
func (img *ExportedImage) UpdateName(name string) {
	img.manifest.RepoTags = []string{name}
}

// PatchMetadata patch the export image's metadata with the src image passed in. Not
// all metadata is patched. We will patch as many as needed.
func (img *ExportedImage) PatchMetadata(src *Image) {
	img.config.Config = src.Config
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

// LoadExportedImage ....
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
