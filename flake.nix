{
  description = "bm-roomba-cpp";

  inputs.nixpkgs.url = "github:nixos/nixpkgs";

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forEachSystem =
        f:
        nixpkgs.lib.genAttrs supportedSystems (
          system:
          f {
            inherit system;
            pkgs = import nixpkgs { inherit system; };
          }
        );
    in
    {
      devShells = forEachSystem (
        { pkgs, system }:
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.base ];
            packages = [
              # pkgs.clang-tools
            ];
          };
        }
      );

      packages = forEachSystem (
        { pkgs, system }:
        let
          viam-cpp-sdk = import ./viam-cpp-sdk.nix { inherit pkgs system; };

          base = pkgs.stdenv.mkDerivation {
            pname = "base";
            version = "0.1";

            src = ./.;

            nativeBuildInputs = [ pkgs.cmake ];

            buildInputs = [
              viam-cpp-sdk.sdk
              pkgs.lgpio
            ];

            cmakeFlags = [ "-DCMAKE_CXX_STANDARD=17" ];
          };
        in
        {
          viam-cpp-sdk = viam-cpp-sdk.sdk;
          inherit base;
          default = base;
        }
      );
    };
}
