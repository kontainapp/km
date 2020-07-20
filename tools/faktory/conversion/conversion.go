package conversion

import (
	"fmt"
	"path"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"kontain.app/km/tools/faktory/image"
	"kontain.app/km/tools/faktory/splitter"
)

func getConversionBase(containerName string) string {
	base := "/tmp/faktory"

	return path.Join(base, containerName)
}

func getOverlayFSBase(base string) string {
	return path.Join(base, "overlayfs")
}

func getTarPath(base string, containerName string) string {
	return path.Join(base, fmt.Sprintf("%s.tar.gz", containerName))
}

// Convert ...
func Convert(containerName string, resultName string, baseName string) error {
	logrus.WithFields(logrus.Fields{
		"container name": containerName,
		"result name":    resultName,
		"base name":      baseName,
	}).Debug("Converting container to kontainer")

	id, err := image.RefnameToID(containerName)
	if err != nil {
		return errors.Wrap(err, "Failed to get image id")
	}

	layers, err := image.GetLayers(id)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers")
	}

	logrus.WithFields(logrus.Fields{
		"name":   containerName,
		"id":     id,
		"layers": layers,
	}).Debug("Get container layers")

	pythonSplitter := splitter.PythonSplitter{}

	keep, err := pythonSplitter.Split(layers)
	if err != nil {
		return errors.Wrap(err, "Failed to split layers")
	}

	logrus.WithFields(logrus.Fields{
		"name":           containerName,
		"id":             id,
		"layers to keep": keep,
	}).Debug("Compute layers to be kept")

	baseID, err := image.RefnameToID(baseName)
	if err != nil {
		return errors.Wrap(err, "Failed to get id from base image")
	}

	baseLayers, err := image.GetLayers(baseID)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers for the base image")
	}

	logrus.WithFields(logrus.Fields{
		"name":   baseName,
		"baseID": baseID,
		"layers": baseLayers,
	}).Debug("Get base layers")

	// We merge the layers to form the layers of the new converted
	// kontainer. We want to graft the kept application layers from the
	// container on top of the base layers.
	merged := append(keep, baseLayers...)

	logrus.WithFields(logrus.Fields{
		"name":   containerName,
		"id":     id,
		"layers": merged,
	}).Debug("Final merged layers")

	conversionBase := getConversionBase(id)
	overlayfsPath := getOverlayFSBase(conversionBase)
	tarPath := getTarPath(conversionBase, id)

	logrus.WithFields(logrus.Fields{
		"conversion_base": conversionBase,
		"overlayfs":       overlayfsPath,
		"tar":             tarPath,
	}).Debug("Conversion Paths")

	if err := mountOverlayFS(merged, overlayfsPath); err != nil {
		return errors.Wrap(err, "Failed to create a merged layers with overlayfs")
	}

	if err := image.Tar(overlayfsPath, tarPath); err != nil {
		return errors.Wrap(err, "Failed to tar the merged rootfs")
	}

	if err := image.ImportImage(tarPath, resultName); err != nil {
		return errors.Wrap(err, "Failed to import docker image")
	}

	if err := unmountOverlayFS(overlayfsPath); err != nil {
		return errors.Wrap(err, "Failed to unmount overlayfs")
	}

	return nil
}
