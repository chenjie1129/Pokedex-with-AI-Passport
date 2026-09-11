# AI Passport 测试版安装指南

本文面向持有兼容 AI Passport 硬件的测试者，用于把 City Spirits
Passport 固件安装到自己的设备。安装不需要手机账号、Wi-Fi 密码或云端服务。

## 1. 支持范围

当前安装包仅支持：

- ESP32-C3；
- 8 MB Flash；
- 已预装 AI Passport bootloader 和本仓库分区表的设备；
- factory app 分区位于 `0x10000`，大小为 `0x300000`。

分区约定：

| 分区 | 地址 | 大小 | 安装策略 |
|---|---:|---:|---|
| NVS | `0x9000` | `0x6000` | 安装前备份，不主动覆盖 |
| factory app | `0x10000` | `0x300000` | 仅更新此分区 |
| Card ID | `0x356000` | `0x4000` | 严禁覆盖 |
| Recovery | `0x700000` | `0x100000` | 严禁覆盖 |

不支持空白 ESP32-C3 开发板或分区布局未知的设备。此类设备需要单独的制造
镜像和硬件验收流程，不能直接套用本指南。

## 2. 安全规则

安装前必须遵守：

1. 不运行 `erase_flash`。
2. 不把测试工程的 bootloader 或 partition table 写入设备。
3. 不写入 `0x8000`、`0x356000` 或 `0x700000`。
4. 只把发布包中的 `Pokedex-AI-Passport.bin` 写到 `0x10000`。
5. 首次安装前备份 factory app 和 NVS，以支持完整回滚。
6. NVS 备份可能包含本地状态，只保存在测试者自己的电脑，不上传到群聊、
   Issue、云盘或测试报告。

当前固件仍包含 Pokémon 名称和素材，只适合受控的内部原型测试。公开发布或
商业分发前必须替换为原创 City Spirits 内容并完成知识产权审核。

## 3. 测试者需要收到的文件

发布负责人应发送一个版本目录：

```text
city-spirits-passport-<git-commit>/
├── Pokedex-AI-Passport.bin
├── SHA256SUMS
└── device-installation-guide.md
```

测试者不需要仓库源码或 ESP-IDF。安装包必须标明：

- Git commit；
- 构建日期；
- 固件 SHA-256；
- 支持的硬件版本；
- 回滚联系人。

## 4. 安装工具

准备一根支持数据传输的 USB-C 线，并安装 Python 3。

macOS 或 Linux：

```sh
python3 -m pip install --user "esptool==4.12.0"
python3 -m esptool version
```

Windows PowerShell：

```powershell
py -m pip install --user "esptool==4.12.0"
py -m esptool version
```

本项目验证使用 `esptool.py v4.12.0`。

## 5. 确认串口

连接设备后查找串口。

macOS：

```sh
ls /dev/cu.usbmodem*
```

Linux：

```sh
ls /dev/ttyACM*
```

Windows：打开“设备管理器”，在“端口”中找到对应的 `COM` 端口。

下面命令中的端口示例：

- macOS：`/dev/cu.usbmodem1101`
- Linux：`/dev/ttyACM0`
- Windows：`COM5`

如果出现多个候选端口，拔掉设备、重新执行命令，再连接设备确认新增项。

## 6. 校验安装包

在固件目录执行：

macOS：

```sh
shasum -a 256 Pokedex-AI-Passport.bin
```

Linux：

```sh
sha256sum Pokedex-AI-Passport.bin
```

Windows PowerShell：

```powershell
Get-FileHash .\Pokedex-AI-Passport.bin -Algorithm SHA256
```

结果必须和 `SHA256SUMS` 完全一致。不一致时停止安装并重新获取文件。

固件大小必须小于 factory app 分区的 `0x300000` 字节（3 MiB）：

```sh
ls -lh Pokedex-AI-Passport.bin
```

## 7. 备份设备

以下示例使用 macOS/Linux。把 `PORT` 改成实际串口：

```sh
PORT=/dev/cu.usbmodem1101
BACKUP_DIR="$HOME/CitySpiritsPassportBackups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"

python3 -m esptool --chip esp32c3 -p "$PORT" flash_id

python3 -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  read_flash 0x10000 0x300000 "$BACKUP_DIR/factory.bin"

python3 -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  read_flash 0x9000 0x6000 "$BACKUP_DIR/nvs.bin"

shasum -a 256 "$BACKUP_DIR/factory.bin" "$BACKUP_DIR/nvs.bin" \
  > "$BACKUP_DIR/SHA256SUMS"
```

Windows PowerShell：

```powershell
$PORT = "COM5"
$BACKUP_DIR = "$HOME\CitySpiritsPassportBackups\$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force -Path $BACKUP_DIR

py -m esptool --chip esp32c3 -p $PORT flash_id

py -m esptool --chip esp32c3 -p $PORT -b 460800 `
  read_flash 0x10000 0x300000 "$BACKUP_DIR\factory.bin"

py -m esptool --chip esp32c3 -p $PORT -b 460800 `
  read_flash 0x9000 0x6000 "$BACKUP_DIR\nvs.bin"

Get-FileHash "$BACKUP_DIR\factory.bin" -Algorithm SHA256
Get-FileHash "$BACKUP_DIR\nvs.bin" -Algorithm SHA256
```

`flash_id` 必须报告 8 MB Flash。型号或容量不一致时停止安装。备份完成前不要
继续。

## 8. 安装固件

macOS 或 Linux：

```sh
PORT=/dev/cu.usbmodem1101
python3 -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x10000 Pokedex-AI-Passport.bin
```

Windows PowerShell：

```powershell
$PORT = "COM5"
py -m esptool --chip esp32c3 -p $PORT -b 460800 `
  --before default_reset --after hard_reset `
  write_flash 0x10000 .\Pokedex-AI-Passport.bin
```

成功时终端会显示 `Hash of data verified`，设备随后自动重启。

如果连接失败，按住设备 BOOT 键，重新插入 USB，看到 `Connecting...` 后松开。
不要因此改用整片擦除。

## 9. 安装后检查

设备应满足：

1. 启动后进入“探索 / 图鉴”首页。
2. 上、下键可以切换选择。
3. OK 可以进入探索或图鉴。
4. 原有图鉴计数仍然存在。
5. 探索、捕捉失败和返回首页不会卡死。

可选串口检查：

```sh
python3 -m serial.tools.miniterm "$PORT" 115200
```

应能看到类似：

```text
BESTIARY_READY schema=3 count=<n> sequence=<n> migrated=<0-or-1>
```

退出串口工具后才能再次烧录。

## 10. 回滚

如果新固件无法启动或旧图鉴无法读取，同时恢复安装前的 factory app 和 NVS：

```sh
PORT=/dev/cu.usbmodem1101
BACKUP_DIR="$HOME/CitySpiritsPassportBackups/<backup-timestamp>"

python3 -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  write_flash 0x9000 "$BACKUP_DIR/nvs.bin"

python3 -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  --after hard_reset write_flash 0x10000 "$BACKUP_DIR/factory.bin"
```

不要恢复其他设备的 NVS。设备密钥、图鉴和未来地点指纹都属于单设备数据。

## 11. 常见问题

| 问题 | 处理 |
|---|---|
| 找不到串口 | 更换支持数据的 USB 线；拔插后重新枚举端口 |
| `Permission denied` | Linux 将当前用户加入 `dialout` 组后重新登录 |
| 一直停在 `Connecting...` | 按住 BOOT，重新插线，连接开始后松开 |
| SHA-256 不一致 | 停止安装，重新下载发布包 |
| 安装后黑屏 | 先硬重启；无效则按第 10 节同时恢复 app 和 NVS |
| 图鉴归零或存档错误 | 不继续捕捉，立即回滚并保留串口日志 |
| 无法再次烧录 | 关闭串口监视器和其他占用端口的软件 |

## 12. 发布负责人流程

发布负责人从已验证 commit 构建，不把本地旧产物直接发给测试者：

```sh
source /path/to/esp-idf/export.sh
idf.py -B build-firmware build
git rev-parse HEAD
shasum -a 256 build-firmware/Pokedex-AI-Passport.bin
```

发布前至少完成：

- `./tools/test-host.sh`；
- ESP32-C3 固件构建；
- 一台设备的 app-only 烧录；
- Flash 回读 SHA-256 对比；
- 启动、图鉴迁移和重启检查；
- 记录 commit、镜像哈希和已知问题。

对于 3–5 人的 T07 测试，建议安排一次 10 分钟远程安装窗口。测试者共享终端，
发布负责人逐条核对端口、备份路径、哈希和启动结果。现阶段不值得先建设云端
账户或 OTA；当测试规模超过约 10 台后，再投入 Web Serial 一键安装器。
