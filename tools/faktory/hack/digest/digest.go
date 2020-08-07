// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

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
