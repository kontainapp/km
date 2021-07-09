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


package main

import (
	"fmt"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"kontain.app/km/tools/faktory/conversion"
	"kontain.app/km/tools/faktory/splitter"
)

func cmdConvert() *cobra.Command {
	var cmd = &cobra.Command{
		Use:   "convert [from] [to] [using as base]",
		Short: "convert from container to kontain using base",
		Args:  cobra.ExactArgs(3),
		PreRun: func(cmd *cobra.Command, args []string) {
			logrus.WithField("args", args).Debug("convert command is called")
		},
		RunE: func(c *cobra.Command, args []string) error {
			containerName := args[0]
			resultName := args[1]
			baseName := args[2]

			if containerName == "" || resultName == "" || baseName == "" {
				return errors.New("Arguements can't be empty")
			}

			splitterType, err := c.Flags().GetString("type")
			if err != nil {
				return errors.Wrap(err, "Failed to get the --type flag")
			}

			if splitterType == "" {
				return errors.New("--type flag is required")
			}

			var s splitter.Splitter
			switch splitterType {
			case "python":
				s = splitter.PythonSplitter{}
			case "java":
				s = splitter.JavaSplitter{}
			default:
				return errors.Errorf("Unsupported type: %s", splitterType)
			}

			converter, err := conversion.NewConverter(baseName, s)
			if err != nil {
				return err
			}

			if err := converter.Convert(containerName, resultName); err != nil {
				return err
			}

			converter.Finished()

			return nil
		},
	}

	cmd.PersistentFlags().String("type", "", "The type of images to convert. Support: java, python.")

	return cmd
}

func main() {
	rootCmd := &cobra.Command{
		Use:          "faktory [commands]",
		Short:        "CLI used to run kontain container",
		SilenceUsage: true,
	}

	rootCmd.PersistentFlags().Bool("debug", false, "Enable debug logging")
	rootCmd.PersistentFlags().String("log-format", "text", "set the format used by logs ('text' or 'json'")

	rootCmd.PersistentPreRunE = func(c *cobra.Command, args []string) error {
		logrus.SetReportCaller(true)

		debug, err := c.Flags().GetBool("debug")
		if err != nil {
			return err
		}

		if debug {
			logrus.SetLevel(logrus.DebugLevel)
		}

		logFormat, err := c.Flags().GetString("log-format")
		if err != nil {
			return err
		}

		switch logFormat {
		case "text":
			// retain the default
		case "json":
			logrus.SetFormatter(new(logrus.JSONFormatter))
		default:
			return fmt.Errorf("Unkown formatter: %s", logFormat)
		}

		return nil
	}

	rootCmd.AddCommand(cmdConvert())

	rootCmd.Execute()
}
