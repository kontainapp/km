package test

import (
	"context"
	"net/http"
	"os/exec"
	"path"
	"testing"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/client"
	"github.com/docker/go-connections/nat"
	"github.com/hashicorp/go-retryablehttp"
	"github.com/pkg/errors"
)

const FROM string = "kontainapp/flask-test/from:latest"
const TO string = "kontainapp/flask-test/to:latest"

var faktoryBin string = path.Join("./", "../../bin/faktory")

func runTest() error {
	retryClient := retryablehttp.NewClient()
	retryClient.RetryMax = 10
	resp, err := retryClient.Get("http://127.0.0.1:8080")
	if err != nil {
		return errors.Wrap(err, "Failed to make the http call")
	}

	if resp.StatusCode != http.StatusOK {
		return errors.New("Didn't get 200")
	}

	return nil
}

// testDocker will test the conversion with a normal container. We use the
// original base as the conversion base. This will make the rootfs of the
// converted image the same as the original.
func testDocker(t *testing.T) error {
	const BASE string = "python:3.8-alpine"

	// Build the from image
	if err := exec.Command("docker", "build", "-t", FROM, "assets/").Run(); err != nil {
		return errors.Wrap(err, "Failed to build the testing image")
	}

	if err := exec.Command(faktoryBin, "--debug", "convert", FROM, TO, BASE).Run(); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	ret, err := cli.ContainerCreate(ctx,
		&container.Config{
			Image:      TO,
			WorkingDir: "/app",
			Cmd:        []string{"gunicorn", "--bind", "0.0.0.0:8080", "app:app"},
			ExposedPorts: nat.PortSet{
				"8080/tcp": {},
			},
		},
		&container.HostConfig{
			AutoRemove: true,
			PortBindings: nat.PortMap{
				"8080/tcp": []nat.PortBinding{
					{
						HostPort: "8080",
					},
				},
			},
		},
		nil,
		"",
	)
	if err != nil {
		return errors.Wrap(err, "Failed to create container")
	}

	if err := cli.ContainerStart(ctx, ret.ID, types.ContainerStartOptions{}); err != nil {
		return errors.Wrap(err, "Failed to start container")
	}

	defer cli.ContainerStop(ctx, ret.ID, nil)

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	exec.Command("docker", "rmi", TO).Run()
	exec.Command("docker", "rmi", FROM).Run()

	return nil
}

func testKontain(t *testing.T) error {
	const BASE string = "kontain/runenv-python:latest"

	// Build the from image
	if err := exec.Command("docker", "build", "-t", FROM, "assets/").Run(); err != nil {
		return errors.Wrap(err, "Failed to build the testing image")
	}

	if err := exec.Command(faktoryBin, "convert", FROM, TO, BASE).Run(); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	ctx := context.Background()
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return errors.Wrap(err, "Failed to create a docker client")
	}

	ret, err := cli.ContainerCreate(ctx,
		&container.Config{
			Image:      TO,
			WorkingDir: "/app",
			Cmd:        []string{"gunicorn", "--bind", "0.0.0.0:8080", "app:app"},
			ExposedPorts: nat.PortSet{
				"8080/tcp": {},
			},
		},
		&container.HostConfig{
			AutoRemove: true,
			PortBindings: nat.PortMap{
				"8080/tcp": []nat.PortBinding{
					{
						HostPort: "8080",
					},
				},
			},
		},
		nil,
		"",
	)
	if err != nil {
		return errors.Wrap(err, "Failed to create container")
	}

	if err := cli.ContainerStart(ctx, ret.ID, types.ContainerStartOptions{}); err != nil {
		return errors.Wrap(err, "Failed to start container")
	}

	defer cli.ContainerStop(ctx, ret.ID, nil)

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	exec.Command("docker", "rmi", TO).Run()
	exec.Command("docker", "rmi", FROM).Run()

	return nil
}

func TestFlask(t *testing.T) {

	t.Run("Test with docker images", func(t *testing.T) {
		if err := testDocker(t); err != nil {
			t.Fatalf("Failed test: %v", err)
		}
	})
	// t.Run("Test with kontain images", func(t *testing.T) {
	// 	if err := testKontain(t); err != nil {
	// 		t.Fatalf("Failed test: %v", err)
	// 	}
	// })

}
