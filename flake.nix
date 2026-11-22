{
  description = "Dagdocdiff Clang and LLVM development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
    in {
      devShells = nixpkgs.lib.genAttrs systems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          llvm = pkgs.llvmPackages_23;
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.ninja
              llvm.clang
              llvm.llvm
              (pkgs.lib.getDev llvm.llvm)
              llvm.libclang
              (pkgs.lib.getDev llvm.clang-unwrapped)
            ];

            shellHook = ''
              export CC=${llvm.clang}/bin/clang
              export CXX=${llvm.clang}/bin/clang++
              export CMAKE_PREFIX_PATH="${pkgs.lib.getDev llvm.llvm}:${pkgs.lib.getDev llvm.clang-unwrapped}:''${CMAKE_PREFIX_PATH:-}"
            '';
          };
        });
    };
}
