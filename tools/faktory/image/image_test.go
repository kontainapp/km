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
