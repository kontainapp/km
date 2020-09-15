package main

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/google/go-github/v32/github"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"golang.org/x/oauth2"
)

func main() {
	rootCmd := &cobra.Command{
		Use:          "release assets...",
		Short:        "CLI used to release km to km-releases",
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

	rootCmd.RunE = func(c *cobra.Command, args []string) error {
		tagName, err := c.Flags().GetString("tag_name")
		if err != nil || tagName == "" {
			return errors.New("--tag_name is required and can't be empty")
		}

		if strings.HasPrefix(tagName, "refs/tags/") {
			tagName = strings.TrimPrefix(tagName, "refs/tags/")
		}

		body, err := c.Flags().GetString("body")
		if err != nil {
			return errors.Wrap(err, "Failed to read --body")
		}

		ctx := context.Background()

		token := os.Getenv("GITHUB_RELEASE_TOKEN")
		if token == "" {
			logrus.Warn("GITHUB_RELEASE_TOKEN is not set")
		}

		ts := oauth2.StaticTokenSource(
			&oauth2.Token{AccessToken: token},
		)
		tc := oauth2.NewClient(ctx, ts)
		client := github.NewClient(tc)

		const (
			owner = "kontainapp"
			repo  = "km-releases"
		)

		newRelease, resp, err := client.Repositories.CreateRelease(ctx, owner, repo,
			&github.RepositoryRelease{
				TagName:    github.String(tagName),
				Name:       github.String(tagName),
				Body:       github.String(body),
				Draft:      github.Bool(false),
				Prerelease: github.Bool(false),
			},
		)
		if err != nil {
			return errors.Wrap(err, "Failed to make the create release request")
		}

		if resp.StatusCode != http.StatusCreated {
			return errors.Wrapf(err, "Failed to get status 201, got %d", resp.StatusCode)
		}

		logrus.WithFields(logrus.Fields{"release id": *newRelease.ID}).Infoln("Created a new release")

		assets := args
		for _, asset := range assets {
			logrus.WithField("asset", asset).Debug("Uploading asset...")
			f, err := os.Open(asset)
			if err != nil {
				return errors.Wrapf(err, "Failed to open asset file: %s", asset)
			}

			assetName := filepath.Base(asset)
			assetExtention := filepath.Ext(assetName)
			assetType := ""
			if assetExtention == "tar.gz" || assetExtention == "zip" {
				assetType = "application/zip"
			}

			createdAsset, resp, err := client.Repositories.UploadReleaseAsset(ctx, owner, repo, *newRelease.ID, &github.UploadOptions{
				Name:      assetName,
				Label:     "",
				MediaType: assetType,
			}, f)
			if err != nil {
				return errors.Wrap(err, "Failed to make the create upload asset request")
			}

			if resp.StatusCode != http.StatusCreated {
				return errors.Wrapf(err, "Failed to get status 201, got %d", resp.StatusCode)
			}

			logrus.WithFields(logrus.Fields{
				"name": asset,
				"url":  createdAsset.GetURL(),
			}).Info("Successfully uploaded asset")
		}

		return nil
	}

	rootCmd.PersistentFlags().String("tag_name", "", "The tag to be released")
	rootCmd.PersistentFlags().String("body", "", "The message for the release")

	if err := rootCmd.Execute(); err != nil {
		logrus.WithError(err).Fatalln("Failed to run release scripts")
	}
}
