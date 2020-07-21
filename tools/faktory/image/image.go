package image

import (
	"context"
	"os"
	"strings"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/client"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

func RefnameToID(refname string) (string, error) {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return "", errors.Wrap(err, "Failed to create a docker client")
	}

	imagelist, err := cli.ImageList(ctx, types.ImageListOptions{All: true})
	if err != nil {
		return "", errors.Wrap(err, "Failed to get a list of images")
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
		return "", errors.New("Image doesn't exist")
	}

	return id, nil
}

func ImportImage(src string, refname string) error {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	f, err := os.Open(src)
	if err != nil {
		return errors.Wrap(err, "Failed to tar to be imported")
	}

	ret, err := cli.ImageImport(
		ctx,
		types.ImageImportSource{
			Source:     f,
			SourceName: "-",
		},
		refname,
		types.ImageImportOptions{
			Platform: "linux",
		},
	)
	if err != nil {
		return errors.Wrap(err, "Failed to import image")
	}

	ret.Close()
	return nil
}

// GetLayers gets a list of layers, in order from top to base using refname
func GetLayers(id string) ([]string, error) {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return nil, errors.Wrap(err, "Failed to create a docker client")
	}

	ret, _, err := cli.ImageInspectWithRaw(ctx, id)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to inspect the image")
	}

	lower := ret.GraphDriver.Data["LowerDir"]

	logrus.WithFields(logrus.Fields{
		"id":                     id,
		"graphdriver.data.lower": lower,
	}).Debug("Reading LowerDir")

	upper := ret.GraphDriver.Data["UpperDir"]

	logrus.WithFields(logrus.Fields{
		"id":                     id,
		"graphdriver.data.upper": upper,
	}).Debug("Reading UpperDir")

	// layers with 0 being the top layer and last index being the most base layer
	layers := []string{upper}
	if lower != "" {
		lowerlist := strings.Split(lower, ":")
		layers = append(layers, lowerlist...)
	}

	logrus.WithFields(logrus.Fields{
		"id":     id,
		"layers": layers,
	}).Debug("Combining Upper and Lower")

	return layers, nil
}
