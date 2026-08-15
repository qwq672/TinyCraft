<p><img src="icon.png" width="100"></p>

# TinyCraft Launcher

**[中文版本](README.md)**

A cross-platform command-line Minecraft launcher written in C, "small but fully featured".

- **Language**: C (`tiny_mc.c` + stripped-down `cJSON.c`)
- **License**: MIT License, Copyright (c) 2026 qwq672
- **Planned platforms**: Windows / ReactOS, Linux, macOS, BSD

## Features

- **Version management**: download, list, and set the default Minecraft version (release / snapshot / old_version / april_fools)
- **Mod loaders**: one-click install of Forge, Fabric, Quilt, NeoForge, and LiteLoader, with automatic dependency completion
- **Accounts**: offline, external (Yggdrasil), and official (reserved) (this version does not yet support Microsoft official login because we have not obtained the Minecraft API credentials (QAQ))
- **Launch**: customizable memory / JVM args / game args, window title, pre-launch command, and console output toggle
- **Incremental download**: downloads only missing or corrupted files, with multi-repository fallback and automatic SNAPSHOT version resolution
- **Auto validation**: corrupted version JSON files are detected and re-downloaded automatically
- **Java management**: auto-scan or manually specify the Java path

## Build

Binary size: ~256 KB on Windows, ~162 KB on Linux.

> Linux dependencies: `libcurl4-openssl-dev`, `zlib1g-dev`. Windows uses WinHTTP (no libcurl needed).

### Build Commands

Windows / ReactOS: `gcc -Os tiny_mc.c cJSON.c -o mc.exe -lwinhttp -lshell32 -luser32 -lz`

Linux: `gcc tiny_mc.c cJSON.c -o mc -lcurl -lz -lm`

macOS / BSD: `clang tiny_mc.c cJSON.c -o mc -lcurl`

## Usage

```bash
mc -ver                 # Show version
mc -help                # Show all command help

# Version management
mc -mcpath <path>       # Set Minecraft directory
mc -lv                  # List installed versions (with mod loaders)
mc -setver <ver>        # Set default version
mc -download -ver <type> <ver>   # Download a version (release/snapshot/old_version/april_fools)
mc -download -mod_loader <loader> [ver]  # Install a mod loader
mc -download -ver_list <type>     # List downloadable versions
mc -download -mod_loader_list <loader>   # List loader versions

# Java
mc -j -au               # Auto-scan for Java
mc -j -list             # List all Java installations

# Accounts
mc -u -l offline <username> [-uuid <uuid>]
mc -u -l external <api_root> <email> <password>
mc -u -l official   # Reserved
mc -u -list             # List accounts
mc -u -del -usertype <index>      # Delete account by index
mc -u -relogin -usertype <index>  # Re-login external account

# Launch
mc -s                   # Quick start (default settings)
mc -start               # Interactive startup
mc -start -ver <name> -account <type,email,pass,server> \
     -java_home <path> -mem 2G -no_verify ...
mc -printstart <ver>    # Export start_mc.bat launch script
```

Advanced launch options: `-ver`, `-account`, `-usertype`, `-java_home`, `-mem`, `-jvm_args`, `-game_args`, `-pre_command`, `-window_title`, `-no_authlib`, `-no_verify`, `-java`. See `tinycraft -help` for details.

## Acknowledgements

- [cJSON](https://github.com/DaveGamble/cJSON) — MIT-licensed lightweight JSON parser
- Networking uses WinHTTP on Windows and libcurl on Unix-like platforms

## License

This project is released under the MIT License. cJSON is released under its own MIT License.
