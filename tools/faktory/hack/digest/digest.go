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


package main

import (
	"fmt"
	"io/ioutil"

	_ "crypto/sha256"

	"github.com/opencontainers/go-digest"
	"github.com/pkg/errors"
	"github.com/spf13/cobra"
)

// A little tool to examine the digest of docker image config files.
func main() {
	rootCmd := &cobra.Command{
		Use:          "digest [container config files]",
		Short:        "CLI used to run kontain container",
		SilenceUsage: true,
		Args:         cobra.ExactArgs(1),
		RunE: func(c *cobra.Command, args []string) error {
			fPath := args[0]

			content, err := ioutil.ReadFile(fPath)
			if err != nil {
				return errors.Wrapf(err, "failed to get digest")
			}

			dgst := digest.FromBytes(content)

			fmt.Printf("%v", dgst)

			return nil
		},
	}

	rootCmd.Execute()
}
