package main

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"kontain.app/km/tools/faktory/conversion"
)

func cmdConvert() *cobra.Command {
	var cmd = &cobra.Command{
		Use:   "convert [container-name]",
		Short: "convert from container to kontain",
		Args:  cobra.ExactArgs(1),
		PreRun: func(cmd *cobra.Command, args []string) {
			logrus.WithField("args", args).Debug("convert command is called")
		},
		RunE: func(c *cobra.Command, args []string) error {
			containerName := args[0]
			baseName, err := c.Flags().GetString("base")
			if err != nil {
				return err
			}

			if err := conversion.Convert(containerName, baseName); err != nil {
				return err
			}

			return nil
		},
	}

	cmd.Flags().String("base", "", "container that will be used as the base")

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
