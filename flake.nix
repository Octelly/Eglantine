# SPDX-FileCopyrightText: Copyright 2026 Eglantine Community
# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "Eglantine";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # officially supported systems
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      # What the commit can say about itself, and nothing more: a flake ref
      # carries the commit and its date, not the branch, the owner or the tag.
      # tools/ci/display-version.sh is the one implementation of the identity
      # format -- it runs at build time here and for the withRef wrapper, and in
      # the CI legs for the tarballs -- so nothing about the format is repeated
      # in Nix. The store version only has to be unique and readable.
      shortRev = self.dirtyShortRev or self.shortRev or "";
      commit = self.rev or "";
      commitTime = self.lastModifiedDate or "";
      releaseVersion = nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);

      # The manifest readers and buildEdenEmulator for each system.
      emulatorTools = forAllSystems (
        system:
        import ./nix/build-eden-emulator.nix {
          lib = nixpkgs.lib;
          pkgs = nixpkgs.legacyPackages.${system};
        }
      );

      mkEglantine =
        system: extra:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          pgo = extra.pgo or false;
          # A store path name takes [A-Za-z0-9+._?=-] only, and it only has to be
          # unique and readable: the human-facing identity is composed by the
          # script during the build, so nothing of the format is repeated here.
          version =
            "${releaseVersion}-${shortRev}" + nixpkgs.lib.optionalString pgo "-pgo";
        in
        pkgs.callPackage ./nix/package.nix (
          {
            inherit commit commitTime version;
            src = ./.;
            buildEdenEmulator = emulatorTools.${system}.buildEdenEmulator;
          }
          // extra
        );
    in
    {
      # A build labelled with the ref it was fetched from. The commit knows its
      # date and its sha; only the consumer knows which ref they asked for, so
      # this wraps the cached, identity-independent binary and hands the full
      # string to it at runtime (EGLANTINE_DISPLAY, see
      # src/common/scm_rev.cpp.in). The wrapper is a script: the heavy artefact
      # stays shared, so this costs a second to build and does not touch the cache.
      #
      # The label is composed by tools/ci/display-version.sh -- the same script
      # the binary's own identity and the tarball legs come from. It runs here
      # because only the consumer knows the ref, and it needs no git because the
      # commit and its time are handed over.
      withRef = forAllSystems (
        system:
        {
          owner,
          repo,
          ref,
          pgo ? false,
        }:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          base = mkEglantine system { inherit pgo; };
        in
        pkgs.symlinkJoin {
          name = "${base.name}-with-ref";
          paths = [ base ];
          nativeBuildInputs = [ pkgs.makeWrapper ];
          postBuild = ''
            display=$(bash ${./tools/ci/display-version.sh} \
              --owner '${owner}' --repo '${repo}' --branch '${ref}' \
              --sha '${commit}' --commit-time '${commitTime}' \
              ${nixpkgs.lib.optionalString pgo "--pgo"})
            echo "labelling the build: $display"
            wrapProgram $out/bin/eglantine --set EGLANTINE_DISPLAY "$display"
          '';
        }
      );

      packages = forAllSystems (system: {
        eglantine = mkEglantine system { };
        eglantine-pgo = mkEglantine system { pgo = true; };
        default = self.packages.${system}.eglantine;
      });

      # buildEdenEmulator, a reusable function, imported here
      lib = forAllSystems (system: emulatorTools.${system});

      overlays.default = final: _prev: {
        eglantine = mkEglantine final.stdenv.hostPlatform.system { };
      };

      # devshell adopted into flake
      # FIXME: Darwin not accounted for, don't believe any current devs use it
      devShells = forAllSystems (
        system:
        nixpkgs.lib.optionalAttrs (!(nixpkgs.lib.hasSuffix "darwin" system)) {
          default = import ./shell.nix { pkgs = nixpkgs.legacyPackages.${system}; };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt-rfc-style);

      # dependencies fetched from the CPM manifest so it stays the source of truth
      checks = forAllSystems (system: {
        cpm-deps = nixpkgs.legacyPackages.${system}.linkFarm "eglantine-cpm-deps" (
          nixpkgs.lib.mapAttrsToList
            (name: path: {
              inherit name path;
            })
            (emulatorTools.${system}.cpmDeps ./.)
        );
      });

      apps = forAllSystems (system: {
        default = self.apps.${system}.eglantine;
        eglantine = {
          type = "app";
          program = "${self.packages.${system}.eglantine}/bin/eglantine";
        };
        eglantine-pgo = {
          type = "app";
          program = "${self.packages.${system}.eglantine-pgo}/bin/eglantine";
        };
      });
    };
}
