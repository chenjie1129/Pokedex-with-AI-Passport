# Pokédex 阶段 2 真机验证报告

状态：`NOT_RUN`
执行日期：
执行人：
复核人：
设备：AI Passport ESP32-C3 8 MiB
设备用途：`SECONDARY_TEST_DEVICE`

> 不填写设备序列号、SSID、BSSID、MAC、凭据、经纬度或备份文件内容。
> full flash 和 NVS 备份只保存在本地，报告只记录 SHA-256。

## 1. 构建身份

| 字段 | 值 |
|---|---|
| Branch | `feat/pokemon-pokedex` |
| Git commit | |
| Firmware version | |
| Build ID | |
| Source SHA-256 | |
| Firmware SHA-256 | |
| Firmware bytes | |
| Factory reserve bytes | |
| ESP-IDF | |
| Dirty | 必须为 `false` |

Host tests：

```text
粘贴测试总数与 pass/fail 摘要，不粘贴私密环境信息。
```

## 2. 设备保护

| 检查 | 结果 | 证据 |
|---|---|---|
| ESP32-C3 | `NOT_RUN` | |
| 8 MiB Flash | `NOT_RUN` | |
| 完整备份 8,388,608 bytes | `NOT_RUN` | |
| 分区 checker 通过 | `NOT_RUN` | |
| 备份 SHA-256 | `NOT_RUN` | 只填 hash |
| 只刷 `0x10000` | `NOT_RUN` | |
| `verify_flash` 通过 | `NOT_RUN` | |
| Card ID 未写入 | `NOT_RUN` | |
| Recovery 未写入 | `NOT_RUN` | |

## 3. 测试结果

允许结果：`PASS`、`FAIL`、`BLOCKED`、`NOT_RUN`。

| ID | 内容 | 级别 | 结果 | 证据/备注 |
|---|---|---|---|---|
| DV-01 | 源码与 Host 基线 | Critical | `NOT_RUN` | |
| DV-02 | Firmware build 和 256 KiB 余量 | Critical | `NOT_RUN` | |
| DV-03 | 打包、SHA 和构建身份 | Critical | `NOT_RUN` | |
| DV-04 | 设备识别、备份和布局 | Critical | `NOT_RUN` | |
| DV-05 | App-only 刷写 | Critical | `NOT_RUN` | |
| DV-06 | 启动和串口 | Critical | `NOT_RUN` | |
| DV-07 | UI、按键和设置 | Required | `NOT_RUN` | |
| DV-08 | 普通存档与重启 | Critical | `NOT_RUN` | |
| DV-09 | 扫描、地点和运行内存 | Critical | `NOT_RUN` | |
| DV-10 | 音频 | Required | `NOT_RUN` | |
| DV-11 | 30 分钟稳定性 | Critical | `NOT_RUN` | |
| DV-12 | 建立 15 条旧存档 | Critical | `BLOCKED` | 等 16 条候选 |
| DV-13 | 首次 15 -> 16 迁移 | Critical | `BLOCKED` | 等 16 条候选 |
| DV-14 | 迁移幂等和 NVS 证据 | Critical | `BLOCKED` | 等 16 条候选 |
| DV-15 | 写入故障/断电原子性 | Critical | `BLOCKED` | 等故障注入构建 |
| DV-16 | NVS 满和垃圾回收 | Critical | `BLOCKED` | 等故障注入构建 |
| SOAK-2H | 两小时稳定性 | Critical | `NOT_RUN` | |
| PRIVACY | 串口隐私扫描 | Critical | `NOT_RUN` | |

## 4. 启动证据

```text
BUILD_ID version=... id=... commit=... source=... dirty=0
BESTIARY_READY schema=... count=... sequence=... migrated=...
PLACE_DATA_READY identity_created=... catalog_found=... places=...
SETTINGS_READY volume=... muted=... brightness=... language=...
```

确认无以下错误：

```text
NVS init failed
Bestiary load failed
LVGL init failed
Guru Meditation
watchdog
panic
```

结果：

## 5. UI 与按键

| 指标 | 标准 | 实测 |
|---|---:|---:|
| Up 操作 | 30 次零漏/双触发 | |
| Down 操作 | 30 次零漏/双触发 | |
| OK 操作 | 30 次零漏/双触发 | |
| 页面往返 | 50 次无异常 | |
| 非扫描可见卡死 | 0 次 > 500 ms | |
| 文字截断/重叠 | 0 | |
| 不可达页面/Back | 0 | |
| 设置重启保持 | 100% | |

问题与照片编号：

## 6. 存档基线

| 数据 | 刷写前 | Round A 后 | 迁移后 | 二次重启 |
|---|---:|---:|---:|---:|
| Seen species | | | | |
| Captured species | | | | |
| Total captures | | | | |
| Owned instances | | | | |
| Buddy instance ID | | | | |
| Places | | | | |
| Passport page | | | | |
| NVS SHA-256 | | | | |

抽样个体：

| Instance ID | Species | HP | ATK | DEF | Personality | Friendship | Memories | 结果 |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| | | | | | | | | |

## 7. 15 -> 16 迁移

Baseline A：

| 字段 | 值 |
|---|---|
| Commit | |
| Firmware SHA-256 | |
| `CITY_SPECIES_COUNT` | 15 |
| Save schema | 12 |

Candidate B：

| 字段 | 值 |
|---|---|
| Commit | |
| Firmware SHA-256 | |
| `CITY_SPECIES_COUNT` | 16 |
| Save schema | |
| First boot `migrated` | 必须为 1 |
| Second boot `migrated` | 必须为 0 |

第 16 条：

| 检查 | 标准 | 实测 |
|---|---|---|
| 可浏览 | 是 | |
| Discovery state | UNKNOWN | |
| Capture count | 0 | |
| Owned count | 0 | |
| 虚假 reward | 0 | |

旧数据差异：

```text
应为“无业务差异”，只允许新增第 16 条 UNKNOWN 和编码格式扩容。
```

## 8. 扫描和内存

同地点 10 次：

| 序号 | kind | eligible | place_id | score | APs | duration_ms | heap_before | heap_after | heap_min |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | | | | | | | | | |
| 2 | | | | | | | | | |
| 3 | | | | | | | | | |
| 4 | | | | | | | | | |
| 5 | | | | | | | | | |
| 6 | | | | | | | | | |
| 7 | | | | | | | | | |
| 8 | | | | | | | | | |
| 9 | | | | | | | | | |
| 10 | | | | | | | | | |

| 指标 | 标准 | 实测 | 结果 |
|---|---:|---:|---|
| 同地点错误新增 | 0/10 | | |
| 跨建筑识别 | >= 9/10 | | |
| 跨建筑确认 | <= 60 s | | |
| 单 pass duration | <= 15,000 ms | | |
| 最小 heap | >= 32,768 bytes | | |
| warm-up 后 heap 漂移 | <= 8,192 bytes | | |
| 最大 DMA block | 需诊断构建 | | |
| OOM/watchdog | 0 | | |

## 9. 音频和稳定性

| 指标 | 标准 | 实测 |
|---|---:|---:|
| 连续音频操作 | 20 次无错误 | |
| 静音立即生效 | 100% | |
| 切页停止旧音频 | 100% | |
| 30 min crash/reset | 0 | |
| 2 h crash/reset | 0 | |
| 2 h 主动扫描 | >= 20 | |
| 2 h 页面切换 | >= 50 | |
| 2 h 持久化操作 | >= 10 | |
| 2 h hard reset | >= 5 | |
| 最终存档一致 | 100% | |

## 10. 故障注入

| 注入点 | UI 未提前发布 | 重启状态 | 重复 reward | 结果 |
|---|---|---|---:|---|
| set 前失败 | | old complete | 0 | |
| set 后/commit 前 | | old complete | 0 | |
| commit 失败 | | old complete | 0 | |
| commit 后 read-back 失败 | | new complete | 0 | |
| commit 后响应前重启 | | new complete | 0 | |
| NVS full | | last commit | 0 | |

## 11. 隐私

| 检查 | 标准 | 实测 |
|---|---:|---:|
| SSID | 0 | |
| BSSID/MAC | 0 | |
| Wi-Fi credentials | 0 | |
| 经纬度 | 0 | |
| full flash/NVS 上传 | 否 | |
| 报告仅含备份 hash | 是 | |

## 12. 缺陷与风险

| ID | 严重度 | 现象 | 复现步骤 | 证据 | 处置 |
|---|---|---|---|---|---|
| | | | | | |

严重度：

- Critical：数据丢失、分区损坏、隐私泄漏、boot loop、虚假奖励；
- High：崩溃、OOM、迁移不幂等、主要流程不可用；
- Medium：可恢复的交互、性能或布局问题；
- Low：不影响功能的轻微问题。

## 13. 最终结论

Round A：`NOT_RUN`

Stage 2 device gate：`BLOCKED`

阻塞项：

- [ ] 16 条 Candidate B
- [ ] 最大 DMA block 诊断
- [ ] fault-injection build
- [ ] NVS full fixture
- [ ] 2 小时 soak

签字：

| 角色 | 结论 | 姓名/账号 | 日期 |
|---|---|---|---|
| 执行人 | | | |
| Firmware review | | | |
| Accountable Gate | | | |
