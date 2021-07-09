// Copyright 2021 Kontain Inc.
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


package splitter

import (
	"os"
	"path"

	"github.com/pkg/errors"
)

// JavaSplitter splits the image for java
type JavaSplitter struct{}

// Split for python requires searching for /usr/bin/python
func (s JavaSplitter) Split(layers []string) ([]string, error) {
	keep := []string{}

	for _, layer := range layers {
		exists, err := s.search(layer)
		if err != nil {
			return nil, errors.Wrap(err, "Failed to search the layer")
		}

		if exists {
			return keep, nil
		}

		keep = append(keep, layer)
	}

	return nil, errors.New("Didn't find Java in any layers")
}

func (JavaSplitter) search(base string) (bool, error) {
	var targets = []string{
		"usr/bin/java",
		"/opt/java/openjdk/bin/java",
		"opt/openjdk-13/bin/java",
		"opt/openjdk-14/bin/java",
	}

	for _, target := range targets {
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
