// Copyright 2021 Kontain Inc
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


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
