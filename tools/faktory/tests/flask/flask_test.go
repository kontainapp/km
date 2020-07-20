package test

import (
	"testing"
)

const NAME string = "flask-test"

func test(t *testing.T) error {
	// Build the from image
	// Build the base image
	// Convert the image
	// Test the function.

	// cmd := exec.Command("tar", "-cvf", destination, src)
	// if err := cmd.Run(); err != nil {
	// 	return errors.Wrap(err, fmt.Sprintf("Failed to tar from %s into %s", src, destination))
	// }

	return nil
}

func TestFlask(t *testing.T) {

	if err := test(t); err != nil {
		t.Fatalf("Failed test: %v", err)
	}

}
