package image

import (
	"context"
	"os"
	"path"
	"strings"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/client"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

func CreateImage(target string) error {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return nil, errors.Wrap(err, "Failed to create a docker client")
	}

	return nil
}

// GetLayers gets a list of layers, in order from top to base using refname
func GetLayers(refname string) ([]string, error) {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return nil, errors.Wrap(err, "Failed to create a docker client")
	}

	imagelist, err := cli.ImageList(ctx, types.ImageListOptions{All: true})
	if err != nil {
		return nil, errors.Wrap(err, "Failed to get a list of images")
	}

	var id string
	for _, img := range imagelist {
		for _, tag := range img.RepoTags {
			if tag == refname {
				id = img.ID[7:]
				break
			}
		}
	}

	if id == "" {
		return nil, errors.New("Image doesn't exist")
	}

	ret, _, err := cli.ImageInspectWithRaw(ctx, id)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to inspect the image")
	}

	lower := ret.GraphDriver.Data["LowerDir"]

	logrus.WithFields(logrus.Fields{
		"name":                   refname,
		"graphdriver.data.lower": lower,
	}).Debug("Reading LowerDir")

	upper := ret.GraphDriver.Data["UpperDir"]

	logrus.WithFields(logrus.Fields{
		"name":                   refname,
		"graphdriver.data.upper": upper,
	}).Debug("Reading UpperDir")

	// layers with 0 being the top layer and last index being the most base layer
	layers := []string{upper}
	if lower != "" {
		lowerlist := strings.Split(lower, ":")
		layers = append(layers, lowerlist...)
	}

	logrus.WithFields(logrus.Fields{
		"name":   refname,
		"layers": layers,
	}).Debug("Combining Upper and Lower")

	return layers, nil
}

func PythonSplitLayers(layers []string) ([]string, error) {
	keep := []string{}

	for _, layer := range layers {
		exists, err := pythonSearch(layer)
		if err != nil {
			return nil, errors.Wrap(err, "Failed to search the layer")
		}

		if exists {
			return keep, nil
		}

		keep = append(keep, layer)
	}

	return nil, errors.New("Didn't find python in any layers")
}

func pythonSearch(base string) (bool, error) {
	var pythonTargets = []string{
		"usr/local/bin/python3",
		"usr/local/bin/python",
	}

	for _, target := range pythonTargets {
		targetPath := path.Join(base, target)

		if _, err := os.Stat(targetPath); err == nil {
			return true, nil
		} else if os.IsNotExist(err) {
			continue
		} else {
			return false, err
		}
	}

	return false, nil
}
