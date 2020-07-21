package test

import (
	"net/http"
	"os/exec"
	"path"
	"testing"

	"github.com/hashicorp/go-retryablehttp"
	"github.com/pkg/errors"
)

const FROM string = "kontainapp/flask-test/from:latest"
const TO string = "kontainapp/flask-test/to:latest"
const BASE string = "python:3.8-alpine"

var faktoryBin string = path.Join("./", "../../", "bin/faktory")

func testDocker(t *testing.T) error {
	// Build the from image
	if err := exec.Command("docker", "build", "-t", FROM, "assets/").Run(); err != nil {
		return errors.Wrap(err, "Failed to build the testing image")
	}

	if err := exec.Command(faktoryBin, "convert", FROM, TO, BASE).Run(); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	gunicornCmd := []string{"gunicorn", "--bind", "0.0.0.0:8080", "app:app"}

	if err := exec.Command("docker", append([]string{"run", "--rm", "-d", "-p", "8080:8080", "-w", "/app", TO}, gunicornCmd...)...).Run(); err != nil {
		return errors.Wrap(err, "Failed to launch converted container")
	}

	retryClient := retryablehttp.NewClient()
	retryClient.RetryMax = 10
	resp, err := retryClient.Get("http://localhost:8080")
	if err != nil {
		return errors.Wrap(err, "Failed to make the http call")
	}

	if resp.StatusCode != http.StatusOK {
		return errors.New("Didn't get 200")
	}

	// Test the function.
	exec.Command("docker", "rmi", TO).Run()

	return nil
}

func TestFlask(t *testing.T) {

	if err := testDocker(t); err != nil {
		t.Fatalf("Failed test: %v", err)
	}

}
