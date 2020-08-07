// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package image

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

const helloworldImage = "docker.io/hello-world:latest"
const helloworldRefname = "hello-world:latest"

func TestRefnameToID(t *testing.T) {
	_, err := RefnameToID(helloworldRefname)
	assert.NoError(t, err, "Failed to get id")
}

func TestGetLayers(t *testing.T) {
	id, err := RefnameToID(helloworldRefname)
	assert.NoError(t, err, "Failed to get id")

	layers, err := GetLayers(id)
	assert.NoError(t, err, "Failed to get layers")
	assert.NotEmpty(t, layers, "Layers should not be empty")
}
