{
  pkgs,
  system,
  # Extension hooks for callers that need to inject additional patches or
  # cmake flags (e.g. cross-compile workarounds). Native builds leave empty.
  extraPostPatch ? "",
  extraCmakeFlags ? [ ],
}:
let
  rustUtilsArch =
    if system == "x86_64-linux" then
      "linux_x86_64"
    else if system == "aarch64-linux" then
      "linux_aarch64"
    else
      throw "unsupported system for rust-utils: ${system}";

  rustUtilsVersion = "0.4.4";

  rustUtilsHashes = {
    "linux_x86_64" = "sha256-ecb6oa7gAI1mySR4sjUT/ftKkpPxzFeWuRbGmN/A89g=";
    "linux_aarch64" = "sha256-Y3+PkdzSsKEgduTw8IctTdS+qp7L8VrSynVYj398pH8=";
  };

  libviam-rust-utils = pkgs.fetchurl {
    url = "https://github.com/viamrobotics/rust-utils/releases/download/v${rustUtilsVersion}/libviam_rust_utils-${rustUtilsArch}.a";
    hash = rustUtilsHashes.${rustUtilsArch};
  };

  viam-rust-utils-header = pkgs.fetchurl {
    url = "https://github.com/viamrobotics/rust-utils/releases/download/v${rustUtilsVersion}/viam_rust_utils.h";
    hash = "sha256-eqVBbJdz3ya21tfp1WwnJxPiEHnvaoksz7W+RY4ewgg=";
  };

  sdkVersion = "0.35.0";

  src = pkgs.fetchFromGitHub {
    owner = "viamrobotics";
    repo = "viam-cpp-sdk";
    rev = "releases/v${sdkVersion}";
    hash = "sha256-hIJadR3TgzbbYrLZkTy0pk6+Vry+0ch49vS08svPSUY=";
  };

  sdkBuildInputs = with pkgs; [
    boost
    grpc
    protobuf
    abseil-cpp
    openssl
    xtensor
    xtl
    xsimd
    nlohmann_json
    zlib
    re2
    c-ares
  ];

  sdkPostPatch = ''
    cp ${libviam-rust-utils} ./libviam_rust_utils-${rustUtilsArch}.a
    cp ${viam-rust-utils-header} ./viam_rust_utils.h
  ''
  + extraPostPatch;

  # SDK .pc.in templates use `''${prefix}/<abs-path>` which becomes
  # `//nix/store/...`. nixpkgs' fixupPhase rejects this. Collapse here.
  # See https://github.com/NixOS/nixpkgs/issues/144170
  sdkPostInstall = ''
    for f in $out/lib/pkgconfig/*.pc; do
      [ -f "$f" ] && sed -i 's|//nix/|/nix/|g' "$f"
    done
  '';

  # FOD: fetches buf modules (network) and runs proto codegen with
  # nixpkgs protoc + grpc_cpp_plugin. Output is the gen/viam/api dir.
  protos = pkgs.stdenv.mkDerivation {
    pname = "viam-cpp-sdk-protos";
    version = sdkVersion;

    inherit src;

    nativeBuildInputs = with pkgs; [
      cmake
      pkg-config
      protobuf
      grpc
      buf
      cacert
    ];

    buildInputs = sdkBuildInputs;

    postPatch = sdkPostPatch;

    preConfigure = ''
      export HOME=$TMPDIR
      export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
    '';

    cmakeFlags = [
      "-DBUILD_SHARED_LIBS=OFF"
      "-DVIAMCPPSDK_BUILD_TESTS=OFF"
      "-DVIAMCPPSDK_BUILD_EXAMPLES=OFF"
      "-DVIAMCPPSDK_USE_WALL_WERROR=OFF"
      "-DVIAMCPPSDK_USE_DYNAMIC_PROTOS=ON"
      "-DVIAMCPPSDK_OFFLINE_PROTO_GENERATION=ON"
      "-DCMAKE_CXX_STANDARD=23"
    ]
    ++ extraCmakeFlags;

    buildPhase = ''
      runHook preBuild
      cmake --build . --target generate-dynamic-protos -j$NIX_BUILD_CORES
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -r src/viam/api/gen/viam/api/. $out/
      runHook postInstall
    '';

    outputHashAlgo = "sha256";
    outputHashMode = "recursive";
    outputHash = "sha256-wCpmx4CP63Jig70zkVouQ/mtGvj/7u2Z+0h4OTh6onM=";
  };

  sdk = pkgs.stdenv.mkDerivation {
    pname = "viam-cpp-sdk";
    version = sdkVersion;

    inherit src;

    postPatch = sdkPostPatch + ''
      # Replace shipped (older-protobuf) static protos with FOD-regenerated ones.
      cp -rf ${protos}/. src/viam/api/
    '';

    nativeBuildInputs = with pkgs; [
      cmake
      pkg-config
      protobuf
      grpc
    ];

    propagatedBuildInputs = sdkBuildInputs;

    cmakeFlags = [
      "-DBUILD_SHARED_LIBS=OFF"
      "-DVIAMCPPSDK_BUILD_TESTS=OFF"
      "-DVIAMCPPSDK_BUILD_EXAMPLES=OFF"
      "-DVIAMCPPSDK_USE_WALL_WERROR=OFF"
      "-DVIAMCPPSDK_USE_DYNAMIC_PROTOS=OFF"
      "-DCMAKE_CXX_STANDARD=23"
    ]
    ++ extraCmakeFlags;

    postInstall = sdkPostInstall;
  };
in
{
  inherit sdk protos;
}
