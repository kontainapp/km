package image

import (
	"fmt"
	"os/exec"

	"github.com/pkg/errors"
)

func Tar(src string, destination string) error {
	cmd := exec.Command("tar", "-cvf", destination, src)
	if err := cmd.Run(); err != nil {
		return errors.Wrap(err, fmt.Sprintf("Failed to tar from %s into %s", src, destination))
	}

	return nil
}
