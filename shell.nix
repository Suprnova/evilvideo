{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    pkgsCross.mingwW64.buildPackages.gcc
    cmake
    ninja
    wineWowPackages.stable
    nasm
    pkg-config
    meson
  ];
}
