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

      # The build identity, composed here from the flake's own metadata.
      #
      # tools/ci/display-version.sh composes it for the tarball legs and has what
      # a flake ref cannot carry: the branch, the owner and the tag. A ref carries
      # the commit and its date, so this form is "<commit time> (<sha7>)" -- the
      # script prints exactly that under --sha-date, and build-nix.yml fails the
      # leg if the two ever disagree.
      #
      # It must not be read from a file. A file written into the tree makes the
      # checkout dirty and the Nix source differ from what a plain
      # `nix build github:owner/repo/ref` sees, so the two evaluate different
      # derivations and the binary cache can never serve the second -- which is
      # the point of building here at all. Nothing reads the clock either: the
      # commit date is stable, so a rebuilt commit still substitutes.
      stamp = self.lastModifiedDate or "";
      shortRev = self.dirtyShortRev or self.shortRev or "";
      identity =
        if builtins.stringLength stamp >= 12 && shortRev != "" then
          "${builtins.substring 0 4 stamp}-${builtins.substring 4 2 stamp}-${builtins.substring 6 2 stamp}_${builtins.substring 8 2 stamp}-${builtins.substring 10 2 stamp} (${shortRev})"
        else
          # No git metadata at all (a source tarball): keep release versioning.
          "v${nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION)}";

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
          display = identity + nixpkgs.lib.optionalString pgo " (PGO)";
          # A store path name takes [A-Za-z0-9+._?=-] only; the " (PGO)" marker
          # becomes the "-pgo" suffix the rest of the tree already uses.
          version =
            nixpkgs.lib.replaceStrings [ " " "(" ")" ] [ "-" "" "" ] identity
            + nixpkgs.lib.optionalString pgo "-pgo";
        in
        pkgs.callPackage ./nix/package.nix (
          {
            inherit display version;
            commit = self.rev or "";
            src = ./.;
            buildEdenEmulator = emulatorTools.${system}.buildEdenEmulator;
          }
          // extra
        );
    in
    {
      # The composed identity, for CI to check against tools/ci/display-version.sh.
      inherit identity;

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
