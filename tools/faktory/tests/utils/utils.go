package utils

import (
	"os"
	"os/exec"

	"github.com/sirupsen/logrus"
)

// RunCommand runs a command using exec.Command. It prints the log to
// stdout/stderr
func RunCommand(name string, args ...string) error {
	cmd := Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// Command is a wrapper of exec.Command with a log
func Command(name string, args ...string) *exec.Cmd {
	logrus.WithFields(logrus.Fields{
		"command": append([]string{name}, args...),
	}).Info("run command")

	cmd := exec.Command(name, args...)
	return cmd
}
