<p><img src="icon.png" width="100"></p>

# TinyCraft Launcher

**[English Ver.](README_EN.md)**

一个用 C 语言编写的跨平台命令行 Minecraft 启动器，秉持“麻雀虽小，五脏俱全”。

- **语言**：C（`tinycraft.c` + 精简版 `cJSON.c`文件）
- **许可证**：MIT License，Copyright (c) 2026 qwq672
- **计划支持的平台**：Windows / ReactOS、Linux、macOS、BSD

## 特性

- **版本管理**：下载、列出、设置默认 Minecraft 版本（正式版 / 快照版 / 远古版 / 愚人节版）
- **Mod 加载器**：一键安装 Forge、Fabric、Quilt、NeoForge、LiteLoader，并自动补全缺失依赖
- **账户系统**：离线（offline）、外置登录（external，Yggdrasil）、正版（official）（此版本暂不支持微软正版登录，因为没有申请到Minecraft API（QAQ））
- **启动游戏**：内存/JVM 参数/游戏参数自定义、窗口标题、预执行命令、控制台输出开关
- **增量下载**：按需下载缺失或损坏的文件，多仓库自动回退，快照（SNAPSHOT）版本自动解析
- **自动验证**：损坏的版本 JSON 会自动识别并重新下载
- **Java 管理**：自动扫描或手动指定 Java 路径

## 编译

编译产物体积：Windows 约 256 KB，Linux 约 162 KB。

> Linux 依赖：`libcurl4-openssl-dev`、`zlib1g-dev`。Windows 使用 WinHTTP（无需 libcurl）。

### 编译命令

Windows / ReactOS: `gcc -Os tinycraft.c cJSON.c -o tinycraft-launcher.exe -lwinhttp -lshell32 -luser32 -lz`

Linux: `gcc tinycraft.c cJSON.c -o tinycraft-launcher -lcurl -lz -lm`

macOS / BSD: `clang tinycraft.c cJSON.c -o tinycraft-launcher -lcurl`

## 使用

```bash
tinycraft-launcher -ver                 # 显示版本
tinycraft-launcher -help                # 显示全部命令帮助

# 版本管理
tinycraft-launcher -mcpath <path>       # 设置 Minecraft 目录
tinycraft-launcher -lv                  # 列出已安装版本（含 Mod 加载器）
tinycraft-launcher -setver <ver>        # 设置默认版本
tinycraft-launcher -download -ver <type> <ver>   # 下载版本（release/snapshot/old_version/april_fools）
tinycraft-launcher -download -mod_loader <loader> [ver]  # 安装 Mod 加载器
tinycraft-launcher -download -ver_list <type>     # 列出可下载版本
tinycraft-launcher -download -mod_loader_list <loader>   # 列出加载器版本

# Java
tinycraft-launcher -j -au               # 自动扫描 Java
tinycraft-launcher -j -list             # 列出所有 Java

# 账户
tinycraft-launcher -u -l offline <username> [-uuid <uuid>]
tinycraft-launcher -u -l external <api_root> <email> <password>
mc -u -l official   # 预留
tinycraft-launcher -u -list             # 列出账户
tinycraft-launcher -u -del -usertype <index>      # 按序号删除账户
tinycraft-launcher -u -relogin -usertype <index>  # 重新登录外置账户

# 启动
tinycraft-launcher -s                   # 快速启动（默认设置）
tinycraft-launcher -start               # 交互式启动
tinycraft-launcher -start -ver <name> -account <type,email,pass,server> \
     -java_home <path> -mem 2G -no_verify ...
tinycraft-launcher -printstart <ver>    # 导出 start_mc.bat 启动脚本
```

启动高级参数：`-ver`、`-account`、`-usertype`、`-java_home`、`-mem`、`-jvm_args`、`-game_args`、`-pre_command`、`-window_title`、`-no_authlib`、`-no_verify`、`-java`。更多细节见 `tinycraft-launcher -help`。



## 致谢

- [cJSON](https://github.com/DaveGamble/cJSON) — MIT 许可的轻量 JSON 解析库
- 本项目网络层在 Windows 使用 WinHTTP、在类 Unix 平台使用 libcurl

## 许可证

本项目基于 MIT License 发布。cJSON 基于其自身的 MIT 许可。
