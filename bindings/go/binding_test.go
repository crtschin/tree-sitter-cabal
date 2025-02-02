package tree_sitter_cabal_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_cabal "gitlab.com/magus/tree-sitter-cabal/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_cabal.Language())
	if language == nil {
		t.Errorf("Error loading Cabal grammar")
	}
}
