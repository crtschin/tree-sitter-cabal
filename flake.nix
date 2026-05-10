{
  inputs = {
    nixpkgs.url = "flake:nixpkgs/nixpkgs-unstable";
    utils.url = "github:numtide/flake-utils";
    cabal-src = {
      url = "github:haskell/cabal";
      flake = false;
    };
    hls-src = {
      url = "github:haskell/haskell-language-server";
      flake = false;
    };
  };

  outputs =
    {
      nixpkgs,
      utils,
      cabal-src,
      hls-src,
      ...
    }:
    utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ ];
        };

        buildTreeSitterPkg =
          { pname, language }:
          pkgs.tree-sitter.buildGrammar {
            inherit language;
            version = "0.1.0";
            src = ./${pname};
            generate = true;
          };

        treeSitterCabal = buildTreeSitterPkg {
          pname = "tree-sitter-cabal";
          language = "cabal";
        };

        treeSitterCabalProject = buildTreeSitterPkg {
          pname = "tree-sitter-cabal-project";
          language = "cabal_project";
        };
      in
      {
        packages = {
          tree-sitter-cabal = treeSitterCabal;
          tree-sitter-cabal-project = treeSitterCabalProject;
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            haskellPackages.cabal-fmt
            tapview
            just
            nodejs
            nixfmt
            prettier
            tree-sitter
            typescript-language-server
          ];
          env.CABAL_SRC = "${cabal-src}";
          env.HLS_SRC = "${hls-src}";
        };
      }
    );
}
