// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package test

import (
	"net/http"
	"os"
	"os/exec"
	"path"
	"testing"

	"github.com/hashicorp/go-retryablehttp"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

const FROM string = "kontainapp/flask-test/from:latest"
const TO string = "kontainapp/flask-test/to:latest"

var faktoryBin string = path.Join("./", "../../bin/faktory")

func runCommand(name string, args ...string) error {
	logrus.WithFields(logrus.Fields{
		"command": append([]string{name}, args...),
	}).Info("run command")

	cmd := exec.Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func runTest() error {
	retryClient := retryablehttp.NewClient()
	retryClient.RetryMax = 3
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
	const BASE string = "python:3.7-alpine"
	const TESTCONTAINER string = "faktory_test_docker"

	// Build the from image
	if err := runCommand("docker", "build", "-t", FROM, "assets/"); err != nil {
		return errors.Wrap(err, "Failed to build the testing image")
	}

	if err := runCommand(faktoryBin, "convert", "--type", "python", FROM, TO, BASE); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	if err := runCommand("docker",
		"run",
		"-d",
		"--rm",
		"-p", "8080:8080",
		"--name", TESTCONTAINER,
		TO); err != nil {
		return errors.Wrap(err, "Failed to create container")
	}

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	if err := runCommand("docker", "stop", TESTCONTAINER); err != nil {
		return errors.Wrap(err, "Failed to stop testing container")
	}

	runCommand("docker", "rmi", TO)
	runCommand("docker", "rmi", FROM)

	return nil
}

func testKontain(t *testing.T) error {
	const BASE string = "kontain/runenv-python:latest"
	const TESTCONTAINER string = "faktory_test_kontain"

	// Build the from image
	if err := runCommand("docker", "build", "-t", FROM, "assets/"); err != nil {
		return errors.Wrap(err, "Failed to build the testing image")
	}

	if err := runCommand(faktoryBin, "convert", "--type", "python", FROM, TO, BASE); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	if err := runCommand("docker",
		"run",
		"-d",
		"--rm",
		"--device=/dev/kvm",
		"-v", "/opt/kontain/bin/km:/opt/kontain/bin/km:z",
		"-p", "8080:8080",
		"--name", TESTCONTAINER,
		TO); err != nil {
		return errors.Wrap(err, "Failed to create container")
	}

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	if err := runCommand("docker", "stop", TESTCONTAINER); err != nil {
		return errors.Wrap(err, "Failed to stop testing container")
	}

	runCommand("docker", "rmi", TO)
	runCommand("docker", "rmi", FROM)

	return nil
}

func TestDocker(t *testing.T) {
	if err := testDocker(t); err != nil {
		t.Fatalf("Failed test: %v", err)
	}
}

func TestKontain(t *testing.T) {
	if err := testKontain(t); err != nil {
		t.Fatalf("Failed test: %v", err)
	}
}
