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

        # Grammars sharing the C scanner symlink src/scanner.c (-> the repo-root
        # ./scanner/scanner.c) and the common grammar helpers (./common) to it.
        # Nix's import of ./${pname} doesn't reach outside that subtree, so we
        # materialize those into real files before handing the source to
        # buildGrammar. `useScanner` / `useCommon` opt a grammar out when it
        # carries neither (e.g. ghc-core).
        buildTreeSitterPkg =
          {
            pname,
            language,
            useScanner ? true,
            useCommon ? true,
          }:
          let
            composedSrc = pkgs.runCommand "${pname}-src" { } ''
              cp -rL ${./.}/${pname} $out
              chmod -R +w $out
              ${pkgs.lib.optionalString useScanner ''
                cp ${./scanner/scanner.c} $out/src/scanner.c
              ''}
              ${pkgs.lib.optionalString useCommon ''
                mkdir -p $out/common
                cp ${./common/utils.mjs} $out/common/utils.mjs
              ''}
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

        treeSitterGhcCore = buildTreeSitterPkg {
          pname = "tree-sitter-ghc-core";
          language = "ghc_core";
          useScanner = false;
          useCommon = false;
        };
      in
      {
        packages = {
          tree-sitter-cabal = treeSitterCabal;
          tree-sitter-cabal-project = treeSitterCabalProject;
          tree-sitter-ghc-core = treeSitterGhcCore;
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
