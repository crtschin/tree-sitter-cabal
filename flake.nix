{
  inputs = {
    nixpkgs.url = "flake:nixpkgs/nixpkgs-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      utils,
    }:
    utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ ];
        };
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            nixfmt
            nodejs
            tree-sitter
            typescript-language-server
            haskellPackages.cabal-fmt
          ];
        };
      }
    );
}
