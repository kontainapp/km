// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package utils

import (
	"os"
	"os/exec"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
)

// RunCommand runs a command using exec.Command. It prints the log to
// stdout/stderr
func RunCommand(name string, args ...string) error {
	cmd := Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return errors.Wrapf(err, "Failed to run %s %v", name, args)
	}

	return nil
}

// Command is a wrapper of exec.Command with a log
func Command(name string, args ...string) *exec.Cmd {
	logrus.WithFields(logrus.Fields{
		"command": append([]string{name}, args...),
	}).Info("run command")

	cmd := exec.Command(name, args...)
	return cmd
}
