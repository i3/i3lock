{
  description = "development environment for i3lock";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          cairo
          libev
          libxkbcommon
          pam
          pkg-config
          xcbutilxrm
          xorg.libX11
          xorg.libxcb
          xorg.xcbutil
          xorg.xcbutilimage
        ];
      };
    };
}
