# gamepad script:
```bash
python3 example/gamepad.py  # connect controller via bluetooth or USB first
```

# Building C++ module
### Standard
```bash
viam module build local ...
```
### Nix
Install [Nix](https://nixos.org/) and enable [flake support](https://wiki.nixos.org/wiki/Flakes#Nix_standalone).
```bash
nix develop  # or use direnv: https://github.com/nix-community/nix-direnv#installation
nix build  # produces result/bin/base
```

## Deploy
#### Nix
Run on same architecture only; eg. pi -> pi.
This will build a self-contained executable and rsync it to the main pi.
```bash
nix run .#deploy
```
