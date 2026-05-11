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
        { pkgs, system, ... }:
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.base ];
            packages = [
              pkgs.clang-tools
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

            cmakeFlags = [ "-DCMAKE_CXX_STANDARD=23" ];
          };

          # Cross-compile to aarch64 with non-system deps overridden to static.
          # libstdc++ and libgcc are also linked statically into base-aarch64
          # (current nixpkgs has no gcc producing Bookworm-compatible
          # libstdc++ symbols), so the binary depends only on libc, libm,
          # libgcc_s (stub), libdl, lgpio at runtime.
          pkgsAarch64 = pkgs.pkgsCross.aarch64-multiplatform.extend (
            final: prev:
            let
              staticCmake = old: {
                cmakeFlags = (old.cmakeFlags or [ ]) ++ [
                  "-DBUILD_SHARED_LIBS=OFF"
                  # Some upstream tests assume shared libs and fail under
                  # BUILD_SHARED_LIBS=OFF (e.g. protobuf 33.5 Arena tests).
                  # Disable test compilation entirely; per-project flag names vary.
                  "-DBUILD_TESTING=OFF"
                  "-Dprotobuf_BUILD_TESTS=OFF"
                  "-DgRPC_BUILD_TESTS=OFF"
                  "-DABSL_BUILD_TESTING=OFF"
                  "-DRE2_BUILD_TESTING=OFF"
                  "-DCARES_BUILD_TESTS=OFF"
                ];
                doCheck = false;
              };
              noTests = old: {
                cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DBUILD_TESTS=OFF" ];
              };
            in
            {
              boost = prev.boost.override {
                enableStatic = true;
                enableShared = false;
              };
              openssl = prev.openssl.override { static = true; };
              zlib = prev.zlib.override {
                shared = false;
                static = true;
              };
              # protobuf has an explicit enableShared toggle; using it avoids
              # the `protobuf_BUILD_SHARED_LIBS=ON` flag the recipe adds.
              # Override both the unversioned alias and protobuf_33 — the cross
              # protobuf depends specifically on buildPackages.protobuf_33.
              protobuf = (prev.protobuf.override { enableShared = false; }).overrideAttrs (old: {
                doCheck = false;
              });
              protobuf_33 = (prev.protobuf_33.override { enableShared = false; }).overrideAttrs (old: {
                doCheck = false;
              });
              abseil-cpp = prev.abseil-cpp.overrideAttrs staticCmake;
              grpc = prev.grpc.overrideAttrs staticCmake;
              # re2 propagates icu unless hostPlatform.isStatic. Strip it —
              # we don't use re2's unicode features, and pulling icu in
              # statically requires extra libicudata wiring.
              re2 = (prev.re2.overrideAttrs staticCmake).overrideAttrs (old: {
                cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DRE2_USE_ICU=OFF" ];
                propagatedBuildInputs = builtins.filter (p: p != prev.icu) (old.propagatedBuildInputs or [ ]);
              });
              # c-ares uses CARES_SHARED/STATIC, not BUILD_SHARED_LIBS.
              c-ares = prev.c-ares.overrideAttrs (old: {
                cmakeFlags = (old.cmakeFlags or [ ]) ++ [
                  "-DCARES_SHARED=OFF"
                  "-DCARES_STATIC=ON"
                ];
              });
              xtl = prev.xtl.overrideAttrs noTests;
              xtensor = prev.xtensor.overrideAttrs noTests;
              xsimd = prev.xsimd.overrideAttrs noTests;
            }
          );

          viam-cpp-sdk-aarch64 = import ./viam-cpp-sdk.nix {
            pkgs = pkgsAarch64;
            system = "aarch64-linux";

            # Cross-grpc's exported cmake config doesn't define the
            # `gRPC::grpc_cpp_plugin` target (the plugin is a build-host
            # binary). Patch the SDK to skip the get_target_property call
            # when we pre-supply the plugin path via cmake flag, and
            # point at buildPackages.grpc for the plugin itself.
            extraPostPatch = ''
              substituteInPlace CMakeLists.txt \
                --replace-fail \
                  'get_target_property(VIAMCPPSDK_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)' \
                  'if (NOT VIAMCPPSDK_GRPC_CPP_PLUGIN)
                  get_target_property(VIAMCPPSDK_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
                endif()'
            '';
            extraCmakeFlags = [
              "-DVIAMCPPSDK_GRPC_CPP_PLUGIN=${pkgsAarch64.buildPackages.grpc}/bin/grpc_cpp_plugin"
            ];
          };

          # Static-libstdc++ pulls in libstdc++ code that uses glibc 2.38+
          # symbols (`__isoc23_strtol` etc. from C23, `pidfd_*` weak refs).
          # Bookworm ships glibc 2.36 and rejects the binary; Trixie has
          # 2.41 and runs it fine. The shim provides weak unversioned
          # definitions that the linker resolves before glibc's versioned
          # ones, keeping GLIBC_2.38/2.39 out of the final binary's verneed.
          # Only needed for Bookworm-targeted builds.
          glibcCompatObj = pkgsAarch64.stdenv.mkDerivation {
            pname = "glibc-compat-obj";
            version = "0.1";
            src = pkgs.writeText "glibc_compat.c" ''
              extern long strtol(const char *, char **, int);
              extern unsigned long strtoul(const char *, char **, int);
              extern long long strtoll(const char *, char **, int);
              extern unsigned long long strtoull(const char *, char **, int);
              extern int vsscanf(const char *, const char *, __builtin_va_list);

              /* Pin our wrappers' calls to the pre-C23 glibc 2.17 versions
                 (aarch64's oldest) so we don't reintroduce 2.38+ refs. */
              __asm__(".symver strtol,strtol@GLIBC_2.17");
              __asm__(".symver strtoul,strtoul@GLIBC_2.17");
              __asm__(".symver strtoll,strtoll@GLIBC_2.17");
              __asm__(".symver strtoull,strtoull@GLIBC_2.17");
              __asm__(".symver vsscanf,vsscanf@GLIBC_2.17");

              __attribute__((weak))
              long __isoc23_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
              __attribute__((weak))
              unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
              __attribute__((weak))
              long long __isoc23_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
              __attribute__((weak))
              unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
              __attribute__((weak))
              int __isoc23_sscanf(const char *str, const char *fmt, ...) {
                __builtin_va_list ap;
                __builtin_va_start(ap, fmt);
                int r = vsscanf(str, fmt, ap);
                __builtin_va_end(ap);
                return r;
              }

              /* libstdc++ references these defensively (process-spawn paths
                 we don't exercise). Weak stubs so they resolve internally. */
              __attribute__((weak))
              int pidfd_spawnp(void *a, void *b, void *c, void *d, void *e, void *f) { return -1; }
              __attribute__((weak))
              int pidfd_getpid(int p) { return -1; }
            '';
            unpackPhase = "cp $src glibc_compat.c";
            buildPhase = "$CC -c -O2 -Wall glibc_compat.c -o glibc_compat.o";
            installPhase = "mkdir -p $out && cp glibc_compat.o $out/";
          };

          mkBaseAarch64 =
            { withGlibcCompatShim ? false }:
            pkgsAarch64.stdenv.mkDerivation {
              pname = "base";
              version = "0.1";

              src = ./.;

              nativeBuildInputs = [ pkgsAarch64.buildPackages.cmake ];

              buildInputs = [
                viam-cpp-sdk-aarch64.sdk
                pkgsAarch64.lgpio
              ];

              cmakeFlags = [
                "-DCMAKE_CXX_STANDARD=23"
              ];

              # cmakeFlags items are word-split by the cmake setup-hook; use
              # cmakeFlagsArray to keep flag values containing spaces intact.
              cmakeFlagsArray = [
                ("-DCMAKE_EXE_LINKER_FLAGS="
                  + (if withGlibcCompatShim then "${glibcCompatObj}/glibc_compat.o " else "")
                  + "-static-libstdc++ -static-libgcc")
              ];

              # Point the binary at the Pi's system dynamic linker / libs,
              # not /nix/store paths that won't exist on the deploy target.
              postFixup = ''
                patchelf --set-interpreter /lib/ld-linux-aarch64.so.1 $out/bin/base
                patchelf --remove-rpath $out/bin/base
              '';
            };

          base-aarch64 = mkBaseAarch64 { };
          base-aarch64-bookworm = mkBaseAarch64 { withGlibcCompatShim = true; };
        in
        {
          viam-cpp-sdk = viam-cpp-sdk.sdk;
          inherit base base-aarch64 base-aarch64-bookworm;
          default = base;
        }
      );
    };
}
