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

        # The per-grammar src/scanner.c is a symlink to ../../scanner/scanner.c
        # at the repo root. Nix's import of ./${pname} doesn't reach outside
        # that subtree, so we materialize the symlink into a real file before
        # handing the source to buildGrammar.
        buildTreeSitterPkg =
          { pname, language }:
          let
            composedSrc = pkgs.runCommand "${pname}-src" { } ''
              cp -rL ${./.}/${pname} $out
              chmod -R +w $out
              cp ${./scanner/scanner.c} $out/src/scanner.c
              mkdir -p $out/common
              cp ${./common/utils.mjs} $out/common/utils.mjs
            '';
          in
          pkgs.tree-sitter.buildGrammar {
            inherit language;
            version = "0.1.0";
            src = composedSrc;
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
          buildInputs =
            with pkgs;
            [
              haskellPackages.cabal-fmt
              hyperfine
              tapview
              just
              nodejs
              nixfmt
              prettier
              tree-sitter
              typescript-language-server
              valgrind
              kdePackages.kcachegrind
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              perf
              flamegraph
            ];
          env.CABAL_SRC = "${cabal-src}";
          env.HLS_SRC = "${hls-src}";
        };
      }
    );
}
