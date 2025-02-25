{
  description = "Mesa";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=release-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };
      in
      {
        devShells.default = pkgs.mkShell.override
          {
            stdenv = pkgs.gccStdenv;
          }
          {
            packages = [ pkgs.mesa-demos ];
            inputsFrom = [ pkgs.mesa ];
          };
      });
}
