// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package splitter

import (
	"os"
	"path"

	"github.com/pkg/errors"
)

// PythonSplitter splits the image for python
type PythonSplitter struct{}

var _ Splitter = (*PythonSplitter)(nil)

// Split for python requires searching for /usr/bin/python
func (ps PythonSplitter) Split(layers []string) ([]string, error) {
	keep := []string{}

	for _, layer := range layers {
		exists, err := ps.search(layer)
		if err != nil {
			return nil, errors.Wrap(err, "Failed to search the layer")
		}

		if exists {
			return keep, nil
		}

		keep = append(keep, layer)
	}

	return nil, errors.New("Didn't find python in any layers")
}

func (PythonSplitter) search(base string) (bool, error) {
	var pythonTargets = []string{
		"usr/local/bin/python3",
		"usr/local/bin/python",
	}

	for _, target := range pythonTargets {
		targetPath := path.Join(base, target)

		if _, err := os.Stat(targetPath); err == nil {
			return true, nil
		} else if os.IsNotExist(err) {
			continue
		} else {
			return false, err
		}
	}

	return false, nil
}
