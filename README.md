<p align="center">
  <a href="https://github.com/skitzellez/xbox-game-vault"><img src="https://raw.githubusercontent.com/skitzellez/xbox-game-vault/main/GameVault/art/logo-banner.png" alt="Game Vault" width="640"></a>
</p>

<h3 align="center">Game Vault · PS Vita</h3>

<p align="center">
  PlayStation Vita games on <b>Xbox Series X|S in Developer Mode</b>, started straight from
  <a href="https://github.com/skitzellez/xbox-game-vault">Game Vault</a>'s PS VITA tab.
</p>

<p align="center">
  <a href="https://github.com/skitzellez/Vita-GameVault/releases/latest"><img alt="DOWNLOAD PS Vita" src="https://img.shields.io/badge/DOWNLOAD-PS%20Vita-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://github.com/skitzellez/xbox-game-vault"><img alt="Get Game Vault" src="https://img.shields.io/badge/GET-Game%20Vault-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://discord.gg/RmhhWgguvw"><img alt="Join the Game Vault Discord" src="https://img.shields.io/badge/JOIN-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white"></a>
</p>

## What it does

- **Press A in Game Vault and play.** Put your Vita games on a USB drive or a network share: a `.vpk`, a `.zip`, or an
  unpacked game folder (named by its ID, e.g. `PCSE00001`). Game Vault lists them with box art, and the first time you
  play one it is installed here by itself (NoNpDrm dumps are decrypted while installing).
- **No setup.** The PS Vita system software is downloaded from PlayStation's own servers the first time.
- **Back to Game Vault** when the game ends.
- **Xbox triggers work:** LT / RT are the Vita's L / R; in Ridge Racer RT is the gas and LT the brake.

Install it with the Game Vault setup, or press **RT** in Game Vault. Not every Vita game runs yet.

## Credits

This is a build of **[Vitox](https://github.com/nirally/Vitox) by nirally**, the PlayStation Vita runtime for Xbox
Series X|S, which runs the **[Vita3K](https://github.com/Vita3K/Vita3K)** emulator core. All the hard work of getting
Vita3K running on the Xbox is theirs; this build adds Game Vault's launch link, the automatic firmware download,
installs from a game folder, and the trigger mapping. Vitox's own README is in [README-VITOX.md](README-VITOX.md).

Please don't take problems with this build to the Vitox or Vita3K teams: use the
[Game Vault Discord](https://discord.gg/RmhhWgguvw) or this repository's Issues.

No games, firmware or keys are included. Use dumps of games you own.

## Licence

GPL-2.0-or-later, like Vita3K and Vitox ([LICENSE](LICENSE)). The source of every release is the matching tag here.
Mesa's Dozen driver (vulkan_dzn.dll, MIT) and the DXIL validator come with the package as Vitox ships them.
