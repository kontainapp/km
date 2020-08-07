// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package conversion

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"golang.org/x/sys/unix"
	dockerimage "kontain.app/km/tools/faktory/docker/image"
	"kontain.app/km/tools/faktory/image"
	"kontain.app/km/tools/faktory/splitter"
)

func tar(src string, destination string) error {
	cmd := exec.Command("tar", "-cvf", destination, "-C", src+"/", ".")
	if err := cmd.Run(); err != nil {
		return errors.Wrap(err, fmt.Sprintf("Failed to tar from %s into %s", src, destination))
	}

	return nil
}

func untar(src string, destination string) error {
	if err := exec.Command("tar", "-xvf", src, "-C", destination).Run(); err != nil {
		return errors.Wrapf(err, "Failed to untar from %s into %s", src, destination)
	}

	return nil
}

// Converter is used to group all the data and methods used to do the convert.
type Converter struct {
	// Path to store artifacts of the conversion process
	root string
	// Path used to fuse the layers together
	fuse string
	// Path used to fix the metadata
	fix string

	// Used to split the layers for fusing
	splitter splitter.Splitter

	// Base image used to make the conversion
	base string

	// This ID is used to identify all the artifacts generated by this converter
	identifier string
}

// NewConverter ...
func NewConverter(base string, splitter splitter.Splitter) (Converter, error) {
	ret := Converter{
		base:     base,
		splitter: splitter,
	}
	dir, err := ioutil.TempDir("", "faktory-")
	if err != nil {
		return ret, errors.Wrap(err, "Failed to create converter fs")
	}

	ret.root = dir
	ret.fuse = filepath.Join(ret.root, "fuse")
	if err := os.MkdirAll(ret.fuse, 0777); err != nil {
		return ret, errors.Wrap(err, "Failed to created fuse dir")
	}

	ret.fix = filepath.Join(ret.root, "fix")
	if err := os.MkdirAll(ret.fix, 0777); err != nil {
		return ret, errors.Wrap(err, "Failed to created fix dir")
	}

	ret.identifier = filepath.Base(ret.root)

	logrus.WithFields(logrus.Fields{
		"Base":       ret.base,
		"Root":       ret.root,
		"Convert ID": ret.identifier,
	}).Info("Creating a new converter")
	return ret, nil
}

// Convert is the main methods to do the convert.
func (c Converter) Convert(from string, to string) error {
	logrus.WithFields(logrus.Fields{
		"from":      from,
		"to":        to,
		"base name": c.base,
	}).Debug("Converting container to kontainer")
	fromID, err := image.RefnameToID(from)
	if err != nil {
		return errors.Wrap(err, "Failed to get image id")
	}

	fromLayers, err := image.GetLayers(fromID)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers")
	}

	keepLayers, err := c.splitter.Split(fromLayers)
	if err != nil {
		return errors.Wrap(err, "Failed to split layers")
	}

	logrus.WithFields(logrus.Fields{
		"name":           from,
		"id":             fromID,
		"layers to keep": keepLayers,
	}).Debug("Compute layers to be kept")

	baseID, err := image.RefnameToID(c.base)
	if err != nil {
		return errors.Wrap(err, "Failed to get id from base image")
	}

	baseLayers, err := image.GetLayers(baseID)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers for the base image")
	}

	// We merge the layers to form the layers of the new converted
	// kontainer. We want to graft the kept application layers from the
	// container on top of the base layers.
	fusedLayers := append(keepLayers, baseLayers...)
	fusedRefname := fmt.Sprintf("%s:fuse", c.identifier)
	if err := c.fuseLayers(fusedLayers, fusedRefname); err != nil {
		return errors.Wrap(err, "Failed to fuse layers")
	}

	if err := c.fixMetadata(fusedRefname, fromID, to); err != nil {
		return errors.Wrap(err, "Failed to fix the metadata")
	}

	if err := image.Delete(fusedRefname); err != nil {
		return errors.Wrap(err, "Failed to clean up the intermediate fused image")
	}

	return nil
}

// Fuse layers into a intermediate container with refname. `docker import` will
// import as an image with refname.
func (c Converter) fuseLayers(layers []string, refname string) error {
	upper := filepath.Join(c.fuse, "upper")
	if err := os.MkdirAll(upper, 0700); err != nil {
		return err
	}

	working := filepath.Join(c.fuse, "work")
	if err := os.MkdirAll(working, 0700); err != nil {
		return err
	}

	merged := filepath.Join(c.fuse, "merged")
	if err := os.MkdirAll(merged, 0700); err != nil {
		return err
	}

	if err := isDuplicate(layers); err != nil {
		return errors.Wrap(err, "fused layers container duplicated layers")
	}

	opts := fmt.Sprintf("lowerdir=%s,upperdir=%s,workdir=%s",
		strings.Join(layers, ":"), upper, working)
	logrus.WithField("overlayfs opts", opts).Debug("mount overlay")
	if err := unix.Mount("overlay", merged, "overlay", 0, opts); err != nil {
		return errors.Wrap(err, "Failed to mount overlay")
	}

	fusedResult := filepath.Join(c.fuse, "fused.tar.gz")

	if err := tar(merged, fusedResult); err != nil {
		return errors.Wrap(err, "Failed to tar the merged rootfs")
	}

	if err := unix.Unmount(merged, 0); err != nil {
		return errors.Wrap(err, "Failed to unmount the overlayfs")
	}

	if err := image.Import(fusedResult, refname); err != nil {
		return errors.Wrap(err, "Failed to import docker image")
	}

	return nil
}

// Fix the metadata of `target` container with the metadata of `src`. The src is
// the original container and target is the converted result. `docker load` the
// image with the resultName.
func (c Converter) fixMetadata(target string, srcID string, resultName string) error {
	targetID, err := image.RefnameToID(target)
	if err != nil {
		return errors.Wrap(err, "Failed to find ID")
	}

	// `docker save` will export a docker image as a tar file. We will need to
	// unpack the tar in order to make edits.
	tarSave := filepath.Join(c.fix, fmt.Sprintf("%s-save.tar.gz", target))
	if err := image.Save(targetID, tarSave); err != nil {
		return errors.Wrap(err, "Failed to save the image")
	}

	tarUnpackSaved := filepath.Join(c.fix, "saved")
	if err := os.MkdirAll(tarUnpackSaved, 0700); err != nil {
		return errors.Wrapf(err, "Failed to create the dir to unpack saved: %s", tarUnpackSaved)
	}

	if err := untar(tarSave, tarUnpackSaved); err != nil {
		return errors.Wrap(err, "Failed to unpack saved")
	}

	// We need to merge the metadata from the source and the target. Loading
	// the unpacked directory into something we can manipulate.
	targetExportedImage, err := dockerimage.LoadExportedImage(tarUnpackSaved)
	if err != nil {
		return errors.Wrap(err, "Failed to load the exported image")
	}

	targetExportedImage.UpdateName(resultName)

	// Source data can be read, but not directly edited.
	srcMetadata, err := image.GetMetadataFromStore(srcID)
	if err != nil {
		return errors.Wrap(err, "Failed to read source metadata")
	}

	baseID, err := image.RefnameToID(c.base)
	if err != nil {
		return errors.Wrap(err, "Failed to get id from base image")
	}

	baseMetadata, err := image.GetMetadataFromStore(baseID)
	if err != nil {
		return errors.Wrap(err, "Failed to read base metadata")
	}

	processedMetadata, err := processMetadata(baseMetadata, srcMetadata)
	if err != nil {
		return errors.Wrap(err, "Failed to process base metadata with src")
	}

	targetExportedImage.PatchMetadata(processedMetadata)

	if err := targetExportedImage.Commit(); err != nil {
		return errors.Wrap(err, "Failed to write the new exported image")
	}

	tarFixed := filepath.Join(c.fix, fmt.Sprintf("%s-fixed.tar.gz", target))
	if err := tar(tarUnpackSaved, tarFixed); err != nil {
		return errors.Wrap(err, "Failed to tar the fixed exported image")
	}

	if err := image.Load(tarFixed); err != nil {
		return errors.Wrap(err, "Failed to load image")
	}

	return nil
}

func processMetadata(base, src *dockerimage.Image) (*dockerimage.Image, error) {
	result, err := dockerimage.NewFromJSON(src.RawJSON())
	if err != nil {
		return nil, errors.Wrap(err, "Failed to make a copy of source metadata")
	}

	result.Config.Env = append(base.Config.Env, src.Config.Env...)

	return result, nil
}

func isDuplicate(layers []string) error {
	hitmap := map[string]struct{}{}

	for _, layer := range layers {
		if _, exist := hitmap[layer]; exist {
			return errors.Errorf("deplicate layers detected: %s", layer)
		}

		hitmap[layer] = struct{}{}
	}

	return nil
}
