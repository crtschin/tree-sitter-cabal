package tree_sitter_cabal_project_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_cabal_project "github.com/crtschin/tree-sitter-cabal-project/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_cabal_project.Language())
	if language == nil {
		t.Errorf("Error loading CabalProject grammar")
	}
}
