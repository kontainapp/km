package image

import (
	"context"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/client"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	dockerimage "kontain.app/km/tools/faktory/docker/image"
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

func Import(src string, refname string) error {
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

func Save(id string, destination string) error {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	reader, err := cli.ImageSave(ctx, []string{id})
	if err != nil {
		return errors.Wrap(err, "Failed to save the image")
	}

	defer reader.Close()

	f, err := os.Create(destination)
	if err != nil {
		return errors.Wrap(err, "Failed to create the file to save the image")
	}

	if _, err := io.Copy(f, reader); err != nil {
		return errors.Wrap(err, "Failed to write to file")
	}

	if err := f.Sync(); err != nil {
		return errors.Wrap(err, "Failed to sync")
	}

	return nil
}

func Load(src string) error {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	f, err := os.Open(src)
	if err != nil {
		return errors.Wrap(err, "Failed to tar to be imported")
	}

	resp, err := cli.ImageLoad(ctx, f, false)
	if err != nil {
		return errors.Wrap(err, "Failed to load the image")
	}

	resp.Body.Close()
	return nil
}

func Delete(refname string) error {
	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	id, err := RefnameToID(refname)
	if err != nil {
		return errors.Wrap(err, "Failed to find ID")
	}

	if _, err := cli.ImageRemove(ctx, id, types.ImageRemoveOptions{
		Force:         true,
		PruneChildren: true,
	}); err != nil {
		return errors.Wrap(err, "Failed to remove image")
	}

	return nil
}

func isTar(path string) bool {
	fileName := filepath.Base(path)
	ext := filepath.Ext(fileName)
	return ext == "tar.gz" || ext == "tar"
}

func GetMetadataFromStore(id string) (*dockerimage.Image, error) {
	const root = "/var/lib/docker/image/overlay2/imagedb"

	store, err := dockerimage.NewStore(root)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to get the docker image store")
	}

	image, err := store.Get(id)
	if err != nil {
		return nil, errors.Wrap(err, "Failed to read the metadata")
	}

	return image, nil
}
