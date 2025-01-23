{
  description = "A Nix-flake-based C/C++ development environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { 
    self, nixpkgs, flake-utils, ...
    }: flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.default = pkgs.mkShell.override {
        stdenv = pkgs.gcc11Stdenv;
      } {
          packages = with pkgs; [
            pkg-config
            ninja
            cmake
            python3
            valgrind
            gcc
            xxd
            lldb
            gdb
          ];

          buildInputs = with pkgs; [
            llvmPackages.clang-tools
            llvmPackages_18.lldb 
          ];
        };
    });
}
