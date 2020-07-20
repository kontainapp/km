package conversion

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"golang.org/x/sys/unix"
)

func upper(target string) string {
	return filepath.Join(target, "upper")
}

func working(target string) string {
	return filepath.Join(target, "work")
}

func merged(target string) string {
	return filepath.Join(target, "merged")
}

func mountOverlayFS(layers []string, target string) error {
	logrus.WithFields(logrus.Fields{
		"layers": layers,
		"target": target,
	}).Debug("Mounting overlayfs")

	upper := upper(target)
	working := working(target)
	merged := merged(target)

	if err := os.MkdirAll(upper, 0700); err != nil {
		return err
	}

	if err := os.MkdirAll(working, 0700); err != nil {
		return err
	}

	if err := os.MkdirAll(merged, 0700); err != nil {
		return err
	}

	opts := fmt.Sprintf("lowerdir=%s,upperdir=%s,workdir=%s",
		strings.Join(layers, ":"), upper, working)

	logrus.WithField("overlayfs opts", opts).Debug("Calling mount overlay")

	if err := unix.Mount("overlay", merged, "overlay", 0, opts); err != nil {
		return errors.Wrap(err, "Failed to mount overlay")
	}

	return nil
}

func unmountOverlayFS(target string) error {
	logrus.WithFields(logrus.Fields{
		"target": target,
	}).Debug("Unmounting overlayfs")

	merged := merged(target)

	if err := unix.Unmount(merged, 0); err != nil {
		return errors.Wrap(err, "Failed to unmount the overlayfs")
	}

	return nil
}
