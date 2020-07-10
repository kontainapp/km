package conversion

import (
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"kontain.app/km/tools/faktory/image"
)

// Convert ...
func Convert(containerName string, baseName string) error {

	logrus.WithFields(logrus.Fields{
		"container name": containerName,
		"base name":      baseName,
	}).Debug("Converting container to kontainer")

	if baseName == "" {
		return errors.New("--base flag is required and can't be empty")
	}

	layers, err := image.GetLayers(containerName)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers")
	}

	logrus.WithFields(logrus.Fields{
		"name":   containerName,
		"layers": layers,
	}).Debug("Get container layers")

	keep, err := image.PythonSplitLayers(layers)
	if err != nil {
		return errors.Wrap(err, "Failed to split layers")
	}

	logrus.WithFields(logrus.Fields{
		"name":           containerName,
		"layers to keep": keep,
	}).Debug("Compute layers to be kept")

	baseLayers, err := image.GetLayers(baseName)
	if err != nil {
		return errors.Wrap(err, "Failed to get layers for the base image")
	}

	logrus.WithFields(logrus.Fields{
		"name":   containerName,
		"layers": baseLayers,
	}).Debug("Get base layers")

	// We merge the layers to form the layers of the new converted
	// kontainer. We want to graft the kept application layers from the
	// container on top of the base layers.
	merged := append(keep, baseLayers...)

	logrus.WithFields(logrus.Fields{
		"name":   containerName,
		"layers": merged,
	}).Debug("Final merged layers")

	if err := mountOverlayFS(merged, "/tmp/faktory/first"); err != nil {
		return errors.Wrap(err, "Failed to create a merged layers with overlayfs")
	}

	return nil
}
