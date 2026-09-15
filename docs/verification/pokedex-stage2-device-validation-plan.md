# Pokédex 阶段 2 真机验证计划

状态：可执行，Round A 可立即开始；Round B 等待 16 条候选固件
适用分支：`feat/pokemon-pokedex`
计划基线提交：`9210586`
目标硬件：AI Passport，ESP32-C3，8 MiB Flash，无 PSRAM
验收负责人（A）：`@chenjie1129`
固件与证据支持（R）：FxxW

## 1. 验证目标

本计划验证阶段 2 的真实硬件行为：

1. 固件只写 factory app 分区，不破坏 NVS、Card ID 和 Recovery；
2. 设备能稳定启动，构建身份、设置、图鉴和地点数据可读取；
3. 捕获、发现、个体、伙伴、HP、设置在重启后保持；
4. 旧 15 条目录升级到 16 条目录时，旧记录按稳定 `species_id` 保留；
5. 新增第 16 条初始化为 UNKNOWN，不产生虚假个体或奖励；
6. 迁移只有在 NVS commit 和 read-back 成功后才对 UI 可见；
7. Wi-Fi 扫描不阻塞 LVGL，内存、扫描耗时和地点识别满足门槛；
8. 长时间操作无崩溃、看门狗、持续 heap 下降或存档回退；
9. 串口日志和验收证据不包含 SSID、BSSID、凭据或精确位置。

## 2. 关键限制

当前 `9210586` 的生产目录仍为 15 条。它包含“读取较小旧目录并升级”的代码，
但没有第 16 个已审核条目，因此：

- **Round A**：可立即验证硬件基线、刷机安全、普通存档、UI、扫描、音频和
  30 分钟稳定性；
- **Round B**：必须使用真实 `CITY_SPECIES_COUNT=16` 的候选固件，才能验证
  15 -> 16 的真机迁移；
- **Round C**：必须使用受控故障注入构建，才能验证 set/commit/read-back
  失败和断电原子性。

随机拔电不是可靠的故障注入。不要在主力设备上通过反复拔线碰运气。Round B
或 Round C 未完成时，只能报告“真机基线通过”，不能报告“阶段 2 真机通过”。

## 3. 人员、环境和时间

| 工作 | 责任人 | 预计时间 |
|---|---|---:|
| 构建、打包和 SHA-256 | FxxW / 测试执行人 | 30 min |
| 设备识别、完整备份和布局校验 | `@chenjie1129` | 20–40 min |
| Round A 功能、扫描和 30 分钟稳定性 | `@chenjie1129` | 90 min |
| Round B 15 -> 16 迁移 | `@chenjie1129` + FxxW | 45 min |
| Round C 故障注入和恢复 | `@chenjie1129` + FxxW | 60 min |
| 最终 2 小时 soak | `@chenjie1129` | 2 h |
| 证据复核和 Gate 决策 | `@chenjie1129` + FxxW | 30 min |

准备：

- 一台非主力 AI Passport；
- 可靠 USB 数据线和直连 USB 端口；
- 电量至少 60%，测试期间供电稳定；
- macOS + Python 3 + ESP-IDF 5.5.3；
- 两个无线环境明显不同的建筑或地点；
- 本地私密证据目录，不使用项目 Git 目录；
- Round C 建议使用可控 USB 电源开关，只在故障注入固件就绪后执行。

## 4. 停止条件

出现以下任一情况立即停止，不继续刷写或重试：

- 芯片不是 ESP32-C3，Flash 不是 8 MiB；
- 完整备份不是 `0x800000` bytes；
- `check_passport_backup.py` 不通过；
- 分区布局与本计划不一致；
- 固件超过 `0x2c0000` bytes；
- 固件或包 SHA-256 不一致；
- 需要运行 `erase_flash`、写 bootloader、partition table、Card ID 或 Recovery；
- 出现 `Guru Meditation`、panic、watchdog reset、反复重启；
- `NVS init failed`、`Bestiary load failed` 或存档状态减少；
- 设备身份、Recovery 或其他设备的备份被误用；
- 串口日志出现原始 SSID、BSSID、Wi-Fi 凭据或精确位置；
- 迁移后需要降级，却没有该设备自己的迁移前 NVS 备份。

## 5. 验收判定

| 结论 | 条件 |
|---|---|
| `ROUND_A_PASS` | DV-01 至 DV-09 中适用于 Round A 的必测项全部通过 |
| `STAGE2_DEVICE_PASS` | Round A、B、C 和 2 小时 soak 全部通过 |
| `BLOCKED` | 缺少 16 条候选、故障注入构建、仪器或必要环境 |
| `FAIL` | 任一 Critical 项失败，或数据/分区/隐私发生回退 |

不接受“基本可用”或“偶尔失败”的条件通过。Optional 项失败可以记录为风险，
Critical 和 Required 项必须全部通过。

## 6. 构建和包验收

### DV-01 源码与 Host 基线（Critical）

```sh
git fetch origin
git switch feat/pokemon-pokedex
git pull --ff-only origin feat/pokemon-pokedex
git status --short
git rev-parse HEAD
./tools/test-host.sh
```

验收标准：

- 分支为 `feat/pokemon-pokedex`；
- 记录完整 commit SHA；
- 用于构建的 worktree 没有未提交或未跟踪文件；
- Host tests 100% 通过；
- 当前已知基线为 32/32，测试数量增加时以实际全通过为准。

若日常工作区不干净，使用独立 clean worktree 构建：

```sh
git worktree add --detach ../Pokedex-device-validation \
  origin/feat/pokemon-pokedex
cd ../Pokedex-device-validation
```

进入用于构建的 clean worktree 后记录：

```sh
REPO="$(pwd)"
```

### DV-02 Firmware build 和容量（Critical）

```sh
. "$IDF_PATH/export.sh"
idf.py -B build/firmware set-target esp32c3
idf.py -B build/firmware \
  -D CITY_CAPTURE_RENDER_SMOKE=OFF \
  -D CITY_AUDIO_RENDER_SMOKE=OFF \
  -D CITY_SETTINGS_SMOKE=OFF build
```

检查 app 大小：

```sh
python3 - <<'PY'
from pathlib import Path
p = Path("build/firmware/Pokedex-AI-Passport.bin")
n = p.stat().st_size
print(f"bytes={n} reserve={0x300000-n}")
assert n <= 0x2c0000, "less than 256 KiB factory reserve"
PY
```

验收标准：

- ESP-IDF 版本为 5.5.3；
- build 成功，无 app partition overflow；
- app `<= 0x2c0000` bytes；
- 相对 3 MiB factory 分区至少保留 256 KiB；
- Recovery 分区大小警告不能被误认为 app 可以写入 Recovery。

### DV-03 打包和身份（Critical）

```sh
PACKAGE_DIR="$HOME/pokedex-device-package-$(date +%Y%m%d-%H%M%S)"
python3 tools/package_firmware.py \
  --build-dir build/firmware \
  --output-dir "$PACKAGE_DIR"
(cd "$PACKAGE_DIR" && shasum -a 256 -c SHA256SUMS)
jq '{version,build_id,git_commit,source_sha256,dirty,firmware_bytes,firmware_sha256}' \
  "$PACKAGE_DIR/manifest.json"
```

验收标准：

- SHA-256 全部通过；
- `dirty=false`；
- `git_commit` 等于 DV-01 记录的 commit；
- `firmware_bytes` 满足 DV-02；
- 包中 app 地址为 `0x10000`；
- 不使用 `--allow-dirty` 作为阶段 Gate 证据。

## 7. 设备保护和刷机

### DV-04 设备身份和完整备份（Critical）

```sh
python3 -m venv .flash-tools
. .flash-tools/bin/activate
python -m pip install esptool==4.12.0
python -m serial.tools.list_ports -v

PASSPORT_PORT=/dev/cu.usbmodem21201
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" flash_id
```

创建仓库外的私密目录：

```sh
EVIDENCE="$HOME/pokedex-device-evidence/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$EVIDENCE"
chmod 700 "$EVIDENCE"

python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  read_flash 0x0 0x800000 "$EVIDENCE/full-flash-before.bin"
python "$PACKAGE_DIR/check_passport_backup.py" \
  "$EVIDENCE/full-flash-before.bin" \
  | tee "$EVIDENCE/backup-check.txt"
chmod 600 "$EVIDENCE/full-flash-before.bin"
shasum -a 256 "$EVIDENCE/full-flash-before.bin" \
  > "$EVIDENCE/full-flash-before.sha256"
```

验收标准：

- `flash_id` 明确显示 ESP32-C3 和 8 MiB；
- 完整备份大小严格为 8,388,608 bytes；
- backup checker 报告布局匹配；
- 以下区域保持受保护：

| 分区 | 地址 | 大小 | 策略 |
|---|---:|---:|---|
| NVS | `0x9000` | `0x6000` | 保留 |
| factory app | `0x10000` | `0x300000` | 唯一允许更新 |
| Card ID | `0x356000` | `0x4000` | 禁止写入 |
| Recovery | `0x700000` | `0x100000` | 禁止写入 |

完整备份和 NVS 数据可能包含设备身份、凭据和私有进度，不得上传、提交 Git 或
发送给他人。报告只记录 SHA-256。

### DV-05 App-only 刷写（Critical）

```sh
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x10000 "$PACKAGE_DIR/Pokedex-AI-Passport.bin"

python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --after hard_reset verify_flash 0x10000 \
  "$PACKAGE_DIR/Pokedex-AI-Passport.bin"
```

验收标准：

- `Hash of data verified`；
- `verify_flash` 通过；
- 命令中只有地址 `0x10000`；
- 不出现 `erase_flash`、`write_flash 0x0` 或分区表写入；
- 刷写后 Card ID 和 Recovery 未发生变化。

在全部测试结束后执行只读对比：

```sh
python3 - "$EVIDENCE" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
full = (root / "full-flash-before.bin").read_bytes()
(root / "cardid-before.bin").write_bytes(full[0x356000:0x35a000])
(root / "recovery-before.bin").write_bytes(full[0x700000:0x800000])
PY

python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  read_flash 0x356000 0x4000 "$EVIDENCE/cardid-after.bin"
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  read_flash 0x700000 0x100000 "$EVIDENCE/recovery-after.bin"
cmp "$EVIDENCE/cardid-before.bin" "$EVIDENCE/cardid-after.bin"
cmp "$EVIDENCE/recovery-before.bin" "$EVIDENCE/recovery-after.bin"
```

两次 `cmp` 必须返回 0。以上四个文件仍属私密设备数据，不得上传。

## 8. Round A：当前固件真机基线

### DV-06 启动和串口（Critical）

记录串口：

```sh
mkdir -p "$EVIDENCE/logs"
idf.py -B "$REPO/build/firmware" -p "$PASSPORT_PORT" monitor 2>&1 \
  | tee "$EVIDENCE/logs/round-a-serial.log"
```

验收标准：

- hard reset 后 15 秒内出现 Home；
- 屏幕标题为 Pokédex；
- `BUILD_ID` 的 version、commit、source、dirty 与 manifest 一致；
- 出现 `BESTIARY_READY`、`PLACE_DATA_READY`、`SETTINGS_READY`；
- 无 `NVS init failed`、`Bestiary load failed`、`LVGL init failed`；
- 无 panic、watchdog、Guru Meditation 或重启循环。

注意：`BESTIARY_READY count=` 当前表示**累计捕获次数**，不是目录物种数。不能
用该字段证明 15 或 16 条目录。

### DV-07 UI、按键和设置（Required）

执行：

1. Up、Down、OK 各操作至少 30 次；
2. 浏览 Home、Pokédex、每一页列表、每个详情页、My Pokémon、Passport、
   Sound & Screen；
3. 修改音量、静音、亮度和语言；
4. 返回 Home，hard reset，再次检查设置；
5. 连续进入/返回页面 50 次。

验收标准：

- 90 次基础按键操作零漏触发、零双触发；
- 非扫描状态下没有超过 500 ms 的可见卡死；
- 页面无文字截断、重叠、越界或不可达 Back；
- 最后一页和第一个/最后一个条目均可访问；
- 中英文切换后内容仍在容器内；
- 设置重启后保持；
- 不出现白屏、黑屏、残影或异常重建。

### DV-08 普通存档与重启（Critical）

准备一个可识别状态：

- 至少捕获 2 个不同物种；
- 额外将 1 个物种保持为 SEEN；
- 选择一个具体个体为 buddy；
- 对该个体产生可见 HP/友情/记忆状态；
- 记录 My Pokémon 数量、个体 ID、属性、HP、来源 place ID；
- 记录 Passport 页和地点数量。

执行：

1. 完成一次捕获，等 UI 明确显示成功；
2. 立即 hard reset；
3. 检查捕获只增加一次；
4. 再次重启 5 次；
5. 检查设置、图鉴、个体、buddy、HP、友情、记忆和地点。

验收标准：

- 每次 UI 成功结果在重启后存在；
- 捕获次数和 owned count 不重复增加；
- 所有稳定 instance ID 不变化；
- 旧个体属性、HP、personality、memory 不变化；
- 最后 buddy 仍指向同一个 instance；
- 五次启动均无迁移重试、存档减少或 fallback；
- 失败或低电状态不显示虚假奖励。

### DV-09 扫描、地点和运行内存（Critical）

同一地点：

1. 完成首次地点确认；
2. 在固定位置主动探索 10 次；
3. 记录每条 `PLACE_RESULT`。

跨建筑：

1. 在 A/B 两个无线环境不同的建筑往返 10 次；
2. 每次到达后主动探索；
3. 记录从点击到确认的时长。

验收标准：

- 每条扫描结果区分 error、empty/sparse 和 usable evidence；
- 同地点 10 次错误新增地点为 0；
- 跨建筑至少 9/10 次在 60 秒内识别变化；
- 单次扫描 pass 的 `duration_ms <= 15000`；
- `heap_min >= 32768` bytes；
- 第 2 次扫描后的 `heap_after` 到第 10 次的下降不超过 8192 bytes；
- 无 `ESP_ERR_NO_MEM`、任务创建失败、watchdog 或 UI 崩溃；
- Scanning/Candidate wait 期间 UI 状态持续更新，没有冻结；
- 同一时刻只有一个扫描请求；
- 地点确认持久化后才出现遭遇。

32 KiB 和 8 KiB 是本阶段的保守初始门槛。首次真机数据完成后可以收紧，不能
因测试失败而临时放宽。

当前日志未输出“最大 DMA-capable 连续块”。阶段 2 最终 Gate 前必须增加该
诊断并补测；Round A 可以记录为 `BLOCKED-DIAGNOSTIC`，不能伪造数值。

### DV-10 音频（Required）

执行：

- 在静音、低、中、高音量分别播放叫声；
- 连续打开/退出详情触发播放 20 次；
- 播放中切页、静音和重启。

验收标准：

- 无持续爆音、卡死、重复尾音或 I2S 错误；
- 静音后不继续播放；
- 切页后旧音频停止；
- 音频压力不导致 UI 卡死、扫描失败或重启；
- 设置重启后保持。

### DV-11 30 分钟稳定性（Critical）

30 分钟内循环：

```text
浏览 5 页 -> 详情 5 次 -> 播放 5 次 -> 扫描 1 次
-> 捕获/返回 -> 设置开关 -> Home
```

验收标准：

- 30 分钟无 crash、panic、watchdog、自动重启；
- 无持续 heap 下降；
- 最终存档重启后完整；
- 电量读数在 0–100 或明确 unavailable，不出现越界；
- 按键和页面响应不随时间明显恶化。

Round A 全部通过后可以记录 `ROUND_A_PASS`，但仍不是阶段 2 真机通过。

## 9. Round B：15 -> 16 真实迁移

### 前置门禁

必须具备两份可追溯包：

- **Baseline A**：15 条目录、schema 12；
- **Candidate B**：16 条目录、包含迁移代码和已批准的第 16 条内容。

两份包分别记录 commit、source SHA、firmware SHA 和 `CITY_SPECIES_COUNT`。
Candidate B 必须通过 Host、render、firmware size 和素材许可检查。

```sh
BASELINE_A_PACKAGE=/absolute/path/to/15-entry-package
CANDIDATE_B_PACKAGE=/absolute/path/to/16-entry-package
```

### DV-12 建立 15 条旧存档（Critical）

在 Baseline A：

1. 完成 DV-08 的复杂状态；
2. 拍摄每页图鉴和 My Pokémon；
3. 保存串口日志；
4. 读取并保存该设备的 NVS 分区：

```sh
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  read_flash 0x9000 0x6000 "$EVIDENCE/nvs-before-migration.bin"
chmod 600 "$EVIDENCE/nvs-before-migration.bin"
shasum -a 256 "$EVIDENCE/nvs-before-migration.bin" \
  > "$EVIDENCE/nvs-before-migration.sha256"
```

验收标准：

- 旧存档可连续重启 3 次；
- 图鉴、owned、buddy、HP、友情、记忆、设置和地点基线已记录；
- 原始 NVS 文件不进入报告或 Git，只记录 hash。

### DV-13 首次迁移启动（Critical）

仅刷 Candidate B app：

```sh
python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  --after hard_reset write_flash 0x10000 \
  "$CANDIDATE_B_PACKAGE/Pokedex-AI-Passport.bin"
```

验收标准：

- 首次启动日志 `BESTIARY_READY ... migrated=1`；
- 所有 15 条旧记录状态、捕获次数和最佳属性不变；
- owned count、instance ID、HP、personality、friendship、memory 不变；
- buddy 仍指向原 instance ID；
- 第 16 条存在且为 UNKNOWN；
- 第 16 条 capture count 为 0、owned count 为 0；
- settings 和地点目录不变；
- UI 只在迁移 commit 和 read-back 成功后进入 ready 状态；
- 无 fallback 到旧 blob、无清空存档。

### DV-14 迁移幂等和 NVS 证据（Critical）

1. Candidate B 首次启动稳定后读取 NVS，计算 hash；
2. 不做任何游戏操作，hard reset；
3. 第二次启动再次读取 NVS，计算 hash。

验收标准：

- 第二次启动 `migrated=0`；
- 两次 Candidate B 启动后的业务状态完全一致；
- 无用户操作时，第二次重启前后 NVS hash 一致；
- blob 已升级为完整 16 条目录；
- 不发生重复捕获、重复个体或 sequence 增长。

## 10. Round C：故障和断电原子性

Round C 需要专用 fault-injection build。当前生产 UI 没有可控 NVS 故障开关，
因此本项在构建就绪前必须报告 `BLOCKED`。

### DV-15 写入失败（Critical）

注入点：

1. `nvs_set_blob` 前失败；
2. `nvs_set_blob` 后、commit 前失败；
3. commit 失败；
4. commit 成功、read-back 失败；
5. commit 成功、UI 收到结果前重启。

验收标准：

- 1–4 均不向 UI 发布新增奖励或迁移完成；
- 重启后只能得到完整旧状态或完整新状态；
- 不允许空存档、半条目、坏 CRC 或重复奖励；
- 第 5 种情况重启后新状态存在且只出现一次；
- 坏 current blob 不能静默 fallback 到陈旧 writable legacy save。

### DV-16 NVS 满和垃圾回收（Critical）

使用测试构建填入最大 16 地点和现实大小的图鉴数据，持续写入直到发生 NVS
page recycle，并注入 full condition。

验收标准：

- full condition 显示存储错误；
- UI 内存模型不提前变化；
- 重启后最后一次已提交状态完整；
- 清理/回收后可以继续写入；
- 无 Card ID、Recovery 或其他 namespace 损坏。

## 11. 最终 2 小时 Soak

在 Candidate B 上执行两小时真实操作：

- 至少 20 次主动扫描；
- 至少 50 次列表/详情切换；
- 至少 20 次音频播放；
- 至少 10 次持久化操作；
- 至少 5 次 hard reset；
- 覆盖同地点和跨建筑；
- 起止各读取一次 NVS hash 和设备状态。

验收标准：

- 无 crash、panic、watchdog、自动重启；
- 无存档丢失、重复 reward 或 instance ID 变化；
- `heap_min >= 32768`；
- warm-up 后 heap 漂移 `<= 8192` bytes；
- 所有页面和按键仍满足 DV-07；
- 结束后完整重启一次，状态与操作记录一致。

## 12. 隐私验收

对串口文本执行：

```sh
LC_ALL=C rg -n -i \
  'ssid|bssid|([0-9a-f]{2}:){5}[0-9a-f]{2}|password|latitude|longitude' \
  "$EVIDENCE/logs"
```

验收标准：

- 原始 SSID、BSSID、MAC、凭据、经纬度命中为 0；
- 日志只包含 AP 数量、score、匿名 place ID、耗时、heap 和错误码；
- 截图不得拍到附近网络列表；
- full flash/NVS 备份不上传；
- evidence report 只引用备份 hash。

## 13. 恢复和降级

应用恢复只写 factory app：

```sh
python3 - "$EVIDENCE" <<'PY'
from pathlib import Path
import sys
evidence = Path(sys.argv[1])
b = (evidence / "full-flash-before.bin").read_bytes()
assert len(b) == 0x800000
(evidence / "factory-before.bin").write_bytes(b[0x10000:0x310000])
PY

python -m esptool --chip esp32c3 --port "$PASSPORT_PORT" --baud 460800 \
  write_flash 0x10000 "$EVIDENCE/factory-before.bin"
```

注意：完成 15 -> 16 迁移后，旧 15 条 app 可能无法读取 16 条 blob。此时仅降级
app 不构成有效恢复。需要由项目负责人从**同一设备**迁移前备份恢复 NVS；
这会丢弃测试后进度。禁止恢复另一台设备的 NVS、Card ID 或完整 flash。

## 14. 验收证据

使用
[`pokedex-stage2-device-validation-report-template.md`](pokedex-stage2-device-validation-report-template.md)
记录：

- 设备型号、Flash 容量，不记录序列号；
- commit、version、source SHA、firmware SHA；
- Host、firmware size、backup checker 和 verify_flash 输出；
- `BUILD_ID`、`BESTIARY_READY`、`PLACE_RESULT` 的脱敏摘录；
- 页面照片和人工检查结果；
- 迁移前后业务计数和 NVS hash；
- heap、duration、稳定性统计；
- 每个测试 ID 的 PASS/FAIL/BLOCKED；
- 残余风险和最终签字。

## 15. 阶段 2 最终门禁

只有以下全部成立，`pokedex-program-status.json` 中阶段 2 才能从
`in_progress` 改为 `completed`：

- DV-01 至 DV-16 的 Critical 项全部 PASS；
- 2 小时 soak PASS；
- 16 条 firmware 保留至少 256 KiB factory 空间；
- LVGL 240×320 render 与真机页面均无布局问题；
- 最大 DMA-capable 连续块已增加诊断并达到基线门槛；
- 素材许可已批准；
- Host、firmware、真机证据分别记录；
- `@chenjie1129` 完成 Gate Review 签字。
