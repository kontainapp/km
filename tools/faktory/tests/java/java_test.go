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
	"path/filepath"
	"testing"

	"github.com/hashicorp/go-retryablehttp"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"kontain.app/km/tools/faktory/tests/utils"
)

const FROM string = "kontainapp/java-test/from:latest"

var assetPath = "assets"
var springbootPath = filepath.Join(assetPath, "gs-rest-service")
var dockerfile = filepath.Join(assetPath, "Dockerfile")

var faktoryBin string = filepath.Join("./", "../../bin/faktory")

func runTest() error {
	url := "http://127.0.0.1:8080/greeting"
	retryClient := retryablehttp.NewClient()
	retryClient.RetryMax = 5
	resp, err := retryClient.Get(url)
	if err != nil {
		return errors.Wrapf(err, "Failed to make the http call: %s", url)
	}

	if resp.StatusCode != http.StatusOK {
		return errors.New("Didn't get 200")
	}

	return nil
}

func setup() error {
	if _, err := os.Stat(springbootPath); err == nil {
		os.RemoveAll(springbootPath)
	}

	os.MkdirAll(springbootPath, 0777)
	springbookAbs, err := filepath.Abs(springbootPath)
	if err != nil {
		return errors.Wrapf(err, "Failed to make springboot path abs path: %s", springbootPath)
	}

	{
		downloadCmd := "git clone -b '2.1.6.RELEASE' --depth 1 https://github.com/spring-guides/gs-rest-service.git ."
		cmd := utils.Command("bash", "-c", downloadCmd)
		cmd.Dir = springbookAbs
		if err := cmd.Run(); err != nil {
			return errors.Wrapf(
				err,
				"Failed to download the springboot. Running [%s] from [%s] ",
				downloadCmd,
				springbookAbs,
			)
		}
	}

	if err := utils.RunCommand("docker", "build", "-t", FROM, "-f", dockerfile, assetPath); err != nil {
		return errors.Wrapf(err, "Failed to build test image. dockerfile %s fs %s", dockerfile, assetPath)
	}

	return nil
}

func cleanup() {
	utils.RunCommand("docker", "rmi", FROM)
	os.RemoveAll(springbootPath)
}

func TestMain(m *testing.M) {
	if err := setup(); err != nil {
		logrus.WithError(err).Fatalln("Failed to setup the test")
	}

	code := m.Run()

	cleanup()

	os.Exit(code)
}

func testDocker() error {
	const BASE string = "adoptopenjdk/openjdk11:alpine-jre"
	const TO string = "kontainapp/java-docker-test/to:latest"
	const TESTCONTAINER string = "faktory_test_docker"

	if err := utils.RunCommand(faktoryBin, "convert", "--type", "java", FROM, TO, BASE); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	if err := utils.RunCommand("docker",
		"run",
		"-d",
		"--rm",
		"-p", "8080:8080",
		"--name", TESTCONTAINER,
		TO); err != nil {
		return errors.Wrapf(err, "Failed to create container %s", TESTCONTAINER)
	}

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	if err := utils.RunCommand("docker", "stop", TESTCONTAINER); err != nil {
		return errors.Wrap(err, "Failed to stop testing container")
	}

	utils.RunCommand("docker", "rmi", TO)

	return nil
}

func TestDocker(t *testing.T) {
	if err := testDocker(); err != nil {
		t.Fatalf("Failed test: %v", err)
	}
}

func testKontain() error {
	const BASE string = "kontain/runenv-jdk-11.0.8:latest"
	const TO string = "kontainapp/java-kontain-test/to:latest"
	const TESTCONTAINER string = "faktory_test_docker"

	if err := utils.RunCommand(faktoryBin, "convert", "--type", "java", FROM, TO, BASE); err != nil {
		return errors.Wrap(err, "Failed to convert")
	}

	if err := utils.RunCommand("docker",
		"run",
		"-d",
		"--rm",
		"--device=/dev/kvm",
		"-v", "/opt/kontain/bin/km:/opt/kontain/bin/km:z",
		"-v", "/opt/kontain/runtime/libc.so:/opt/kontain/runtime/libc.so:z",
		"-p", "8080:8080",
		"--name", TESTCONTAINER,
		TO); err != nil {
		return errors.Wrapf(err, "Failed to create container %s", TESTCONTAINER)
	}

	if err := runTest(); err != nil {
		return errors.Wrap(err, "Failed to test the converted image")
	}

	if err := utils.RunCommand("docker", "stop", TESTCONTAINER); err != nil {
		return errors.Wrap(err, "Failed to stop testing container")
	}

	utils.RunCommand("docker", "rmi", TO)

	return nil
}

func TestKontain(t *testing.T) {
	if err := testKontain(); err != nil {
		t.Fatalf("Failed test: %v", err)
	}
}
