{
  inputs = {
    nixpkgs.url = "flake:nixpkgs/nixpkgs-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      utils,
      ...
    }:
    utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ ];
        };

        pkgDirs = [
          "tree-sitter-cabal"
          "tree-sitter-cabal-project"
        ];
        pkgDirsStr = builtins.concatStringsSep " " pkgDirs;

        nodeModules = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "tree-sitter-cabal-workspace-node-modules";
          version = "0.0.0";
          src = ./.;

          pnpmDeps = pkgs.fetchPnpmDeps {
            inherit (finalAttrs) pname version src;
            fetcherVersion = 3;
            hash = "sha256-FK8Q2T6l2r5l1e49gHKfpx1JugHSsNRG9vEBgNMAmbI=";
          };

          nativeBuildInputs = with pkgs; [
            (node-gyp.override { nodejs = nodejs_22; })
            nodejs_22
            pnpm
            pnpmConfigHook
            python3
          ];

          env.npm_config_nodedir = pkgs.nodejs_22;

          buildPhase = ''
            runHook preBuild
            for treeSitterDir in node_modules/.pnpm/tree-sitter@*/node_modules/tree-sitter; do
              if [ -d "$treeSitterDir" ]; then
                pushd "$treeSitterDir"
                node-gyp rebuild
                popd
              fi
            done
            for pkgDir in ${pkgDirsStr}; do
              pushd "$pkgDir"
              node-gyp rebuild
              popd
            done
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p $out
            cp -a node_modules $out/node_modules
            for pkgDir in ${pkgDirsStr}; do
              mkdir -p $out/$pkgDir
              cp -a $pkgDir/node_modules $out/$pkgDir/node_modules
              cp -a $pkgDir/build        $out/$pkgDir/build
            done
            runHook postInstall
          '';
        });
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            (node-gyp.override { nodejs = nodejs_22; })
            haskellPackages.cabal-fmt
            just
            nixfmt
            nodejs_22
            pnpm
            prettier
            python3
            tree-sitter
            typescript-language-server
          ];

          env.npm_config_nodedir = pkgs.nodejs_22;

          shellHook = ''
            ln -snf ${nodeModules}/node_modules ./node_modules
            for pkgDir in ${pkgDirsStr}; do
              ln -snf ${nodeModules}/$pkgDir/node_modules ./$pkgDir/node_modules
              mkdir -p ./$pkgDir/build/Release
              binding="tree_sitter_$(echo "$pkgDir" | tr - _)_binding.node"
              ln -snf ${nodeModules}/$pkgDir/build/Release/$binding ./$pkgDir/build/Release/$binding
            done
          '';
        };
      }
    );
}
