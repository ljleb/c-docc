{
  description = "Dagdocdiff C and C++ dependency diff";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      toolsFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          llvm = pkgs.llvmPackages_23;
        in {
          inherit pkgs llvm;
          buildTools = [ pkgs.cmake pkgs.ninja llvm.clang ];
          libraries = [
            pkgs.cmark
            pkgs.utf8proc
            (pkgs.lib.getDev llvm.llvm)
            llvm.libclang
            (pkgs.lib.getDev llvm.clang-unwrapped)
          ];
        };
    in {
      packages = forAllSystems (system:
        let tools = toolsFor system;
        in {
          default = tools.pkgs.stdenv.mkDerivation {
            pname = "dagdocdiff";
            version = tools.pkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);
            src = tools.pkgs.lib.cleanSourceWith {
              src = ./.;
              filter = path: _: !(builtins.elem (baseNameOf path) [ ".direnv" "build" "result" ".git" ]);
            };
            nativeBuildInputs = tools.buildTools;
            buildInputs = tools.libraries;
            cmakeFlags = [
              "-DCMAKE_C_COMPILER=${tools.llvm.clang}/bin/clang"
              "-DCMAKE_CXX_COMPILER=${tools.llvm.clang}/bin/clang++"
            ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              ctest --output-on-failure
              runHook postCheck
            '';
            meta.mainProgram = "dagdocdiff";
          };
        });

      devShells = forAllSystems (system:
        let
          tools = toolsFor system;
          inherit (tools) pkgs llvm;
        in {
          default = pkgs.mkShell {
            packages = tools.buildTools ++ tools.libraries ++ [ llvm.llvm ];
            shellHook = ''
              export CC=${llvm.clang}/bin/clang
              export CXX=${llvm.clang}/bin/clang++
              export CMAKE_PREFIX_PATH="${pkgs.lib.getDev llvm.llvm}:${pkgs.lib.getDev llvm.clang-unwrapped}:''${CMAKE_PREFIX_PATH:-}"
            '';
          };
        });
    };
}
