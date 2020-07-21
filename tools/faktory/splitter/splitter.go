package splitter

// Splitter splits the image layers and return the layers need to be kept
type Splitter interface {
	Split(layers []string) ([]string, error)
}
