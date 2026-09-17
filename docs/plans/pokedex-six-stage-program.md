# Pokédex 六阶段实施计划

状态：执行中
启动日期：2026-09-15
发布分支：`main`
项目负责人（A）：`@chenjie1129`
实施与证据整理（R）：`FxxW`

## 1. 执行原则

本计划默认串行执行。2026-09-17 负责人明确授权一个例外：阶段 2 真机验证延期，
保持未完成状态，允许提交到 main 并启动阶段 3 开发。此决定不代表阶段 2 验收通过；
详见 [延期记录](../verification/pokedex-stage2-deferral-2026-09-17.md)。其他阶段门禁不变。
阶段 4–6 还必须通过 P0 产品门槛：地点相关发现已被真实用户证明能够提高重复
携带和再次探索。未通过时，后端、微服务和多区域建设保持 `BLOCKED`，不以
排期压力绕过产品判断。

职责标记采用 RACI：

- `A`：最终负责并批准阶段门禁；
- `R`：直接实施并准备证据；
- `C`：提供架构、安全、内容或测试评审；
- `I`：接收进度和风险同步。

在负责人另行授权前，`@chenjie1129` 是所有阶段的临时 Accountable DRI；
FxxW 可以实施和验证自动化工作，但不能代替人工完成产品门槛、版权审批或
真机验收。

## 2. 统一进度机制

### 2.1 节奏

| 频率 | 活动 | 输出 | 责任人 |
|---|---|---|---|
| 每次代码变更 | 自动门禁 | Host tests、schema、diff check | FxxW |
| 每个工作日 | 阶段状态更新 | 完成项、下一步、阻塞、风险变化 | FxxW |
| 每周一 | 风险评审 | 风险等级、触发器、缓解动作 | `@chenjie1129` + FxxW |
| 每个阶段末 | Gate Review | 验收证据、残余风险、是否进入下一阶段 | `@chenjie1129` |
| 每次真机发布 | 硬件验收 | firmware size、heap、NVS、LVGL、实体按键 | 人工设备测试人 |

进度状态保存在
[`docs/verification/pokedex-program-status.json`](../verification/pokedex-program-status.json)，
风险台账保存在
[`docs/verification/pokedex-program-risk-register.json`](../verification/pokedex-program-risk-register.json)。
状态更新必须包含日期、完成百分比、证据和下一项动作，不能只写“进行中”。

### 2.2 通用完成定义

一个阶段只有同时满足以下条件才能标记 `COMPLETED`：

1. 所有阶段验收项有可追溯证据；
2. `./tools/test-host.sh` 通过；
3. `git diff --check` 通过；
4. 相关 schema、API 和迁移有失败路径测试；
5. 隐私规则没有回退；
6. firmware build 与真机测试单独报告；
7. Gate Review 由 Accountable DRI 批准；
8. 后续阶段的输入合同已固定。

## 3. 阶段 1：P0 基线与治理

### 目标

冻结当前嵌入式单体的真实边界、数据口径、证据合同和六阶段治理方式，使后续
扩展有可比较基线。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / 产品门槛 | `@chenjie1129` |
| Firmware / Domain 实施 | FxxW |
| Host 验证 | FxxW |
| 真机基线签字 | `@chenjie1129` |

目标完成时间：2026-09-16。

### 工作项

- [x] 建立图鉴分类、来源和统计框架；
- [x] 固定 PokéAPI commit 和官方 #1025 尾项核验；
- [x] 建立容量评估和“理论无上限”演进文档；
- [x] 建立六阶段计划、状态、风险和证据合同；
- [x] 把研究同步器纳入离线 Host 测试；
- [ ] 补录当前固件 build size、最小 heap 和实体设备基线。

### 验收标准

自动验收：

- Host tests 全部通过；
- 来源快照 `--check` 通过；
- 当前分支是 `feat/pokemon-pokedex`；
- 阶段状态严格按 1–6 顺序；
- 风险台账中每项有 owner、trigger 和 mitigation。

人工验收：

- 当前 P0 目标和不做项获得确认；
- 固件 build 与真机结果单独签字。

阶段 1 可在 Host 范围标记完成；缺失的硬件数据作为阶段 2 发布阻塞项，不能
被 Host 通过替代。

### 交接到阶段 2

输入合同：

- 稳定 `species_id` 是存档含义，不使用数组 index；
- 新增目录条目必须保留旧记录；
- 缺失的新记录初始化为 UNKNOWN；
- 迁移必须 durable commit + read-back 后才能发布到 UI。

## 4. 阶段 2：稳定身份与存档扩容迁移

### 目标

解除存档对“当前编译物种数量必须与旧 blob 完全一致”的依赖，先完成
15 -> 16 目录扩容的可靠迁移，再提取 catalog provider 边界。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / Firmware DRI | `@chenjie1129` |
| Codec 和 Host 测试 | FxxW |
| BSP/NVS 与真机故障测试 | `@chenjie1129` + FxxW |
| 内容 ID 审核 | `@chenjie1129` |

目标完成时间：2026-09-30。

### 工作项

- [x] 启动：按稳定 ID 解码较小旧目录；
- [x] 启动：缺失当前条目初始化为 UNKNOWN；
- [x] 启动：BSP 将目录数量变化识别为 migration；
- [x] 启动：迁移后写入完整当前 blob 并读回比对；
- [x] 增加第 16 个原创候选 Mossbit（CC0 素材，不进入遭遇池）；
- [x] 提取 `city_catalog_provider_t`；
- [x] 记录 schema/catalog revision 兼容矩阵；
- [ ] 完成真实 NVS 满、断电和重启测试；
- [x] 完成 16 条 firmware build 与 LVGL fixture。

### 验收标准

- 旧目录记录可乱序并按 stable ID 映射；
- 新目录记录为 UNKNOWN 且不伪造 owned instance；
- 重复 ID、未知 ID、未来目录数量、坏 CRC 全部拒绝；
- migration 写入失败不改变 UI 模型；
- commit 成功但 read-back 失败不发布；
- 再次启动读取升级后 blob，不重复迁移；
- Host、firmware、render、NVS 和实体设备证据全部齐全。

### 升级触发

原始触发条件是 16 条内容证明 Flash 或更新频率成为瓶颈。2026-09-17 负责人已
明确授权启动阶段 3；当前干净镜像在保留 256 KiB 余量后仅剩 45,120 字节用于扩展。
阶段 2 剩余真机检查继续标记未完成。

## 5. 阶段 3：签名内容包与有界缓存

### 目标

把内容发布与 firmware 解耦，同时保持设备核心离线、有界 RAM/Flash 和可回滚。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / Device Release DRI | `@chenjie1129` |
| Package format / provider | FxxW |
| 密钥与签名审批 | `@chenjie1129` |
| 真机性能 | 人工设备测试人 |

目标完成时间：2026-10-21。已按 2026-09-17 负责人例外决定启动开发；
第一增量是 Host 内容包格式、签名和校验，设备加载/回滚/缓存另行验证。

### 工作项

- [x] 第一增量：Host compact binary container、Ed25519 签名和流式 SHA-256 校验；
- [x] Typed metadata/asset profile 与有界 C reader，Host 真签名校验和 ESP32-C3 编译；
- [ ] 设备 crypto/trusted key、实际存储/cache 与运行时资源验证；
- [ ] 将签名校验接入设备包验证与安装路径；
- 实现 compiled provider 与 package provider；
- 实现 active/rollback manifest 原子切换；
- 实现 Flash 有界 LRU；
- [x] Host 100、1,000、10,000 条合成容器校验与有界 Python 分配测试；
- [ ] 设备分页和缓存淘汰测试（Host 容器测试不能替代）；
- 保持 recovery、cardid 和 NVS 分区合同。

### 验收标准

- 任意单字节损坏、错误签名、缺失对象都不能切换 active pack；
- 中断下载后旧包仍可离线使用；
- LVGL 只创建 4 个 row；
- RAM 不随全量目录线性增长；
- 设备无网络时已安装内容可完整运行；
- firmware 和真机长稳测试通过。

### 交接到阶段 4

必须同时满足：

- 内容更新确实提高回访或降低固件发布成本；
- P0 携带价值门槛由 `@chenjie1129` 人工批准；
- 公开内容包的版权和素材许可明确。

## 6. 阶段 4：模块化单体内容后端

### 目标

在产品门槛通过后，提供目录、素材、发布、搜索读模型和可选同步的单部署单元，
避免过早微服务化。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / Interim Backend DRI | `@chenjie1129` |
| 架构和接口实现 | FxxW + 后续指定 Backend DRI |
| 数据治理与版权 | `@chenjie1129` |
| 运维和恢复 | 后续指定 SRE DRI |

条件目标时间：2026-11-25。P0 gate 未通过时状态保持 `BLOCKED`，日期自动冻结，
不计为延期。

### 技术范围

- PostgreSQL 元数据；
- 对象存储不可变素材；
- Redis 热点缓存；
- CDN 内容分发；
- outbox + worker；
- REST/JSON 公共 API；
- 两个无状态应用实例。

### 验收标准

- 1M synthetic entries；
- 1,000 RPS，P95 < 300 ms；
- 99.9% 可用性设计；
- RPO <= 5 min，RTO <= 30 min；
- 幂等同步不重复捕获；
- 备份恢复和索引重建演练通过。

## 7. 阶段 5：水平扩展与领域服务化

### 目标

当模块化单体的 CPU、IO、连接池或故障域达到阈值后，先无状态水平扩展，再按
明确边界拆 Catalog、Asset、Progress、Search 和 Publish。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / Interim Platform DRI | `@chenjie1129` |
| 服务与事件契约 | 后续指定 Backend DRI |
| SLO / tracing / on-call | 后续指定 SRE DRI |
| 负载与故障测试 | FxxW + QA DRI |

条件目标时间：2027-01-15。

### 进入条件

- 阶段 4 主库 CPU/IO 峰值持续 > 70%；
- API P95 连续 15 分钟 > 300 ms；
- 发布、搜索、同步出现可证明的独立扩缩容需求；
- 已具备 tracing、schema registry、值班和 error budget。

### 验收标准

- 100M synthetic entries；
- 10,000 RPS 起步并可按实例线性扩展；
- CDN hit >= 95%；
- 核心 API 99.95%；
- outbox/event 可重复处理；
- 搜索故障不影响权威目录和玩家进度；
- 单实例和单副本故障注入通过。

## 8. 阶段 6：分片、多区域与联邦目录

### 目标

只有单区域容量、RTO 或合规要求真实出现后，按 namespace/catalog 分片内容，
按 player_id 分片玩家状态，并引入 home region。

### 责任与时限

| 角色 | 负责人 |
|---|---|
| Accountable / Interim SRE DRI | `@chenjie1129` |
| 分片与迁移 | 后续指定 Database DRI |
| 多区域容灾 | 后续指定 SRE DRI |
| 合规与隐私 | `@chenjie1129` |

条件目标时间：2027-03-31。

### 进入条件

- 阶段 5 单集群已达到 60–70% 安全容量；
- 单区域 RTO 不满足业务要求；
- 存在明确的数据驻留或多发行方隔离需求；
- shard split、dual-read 和恢复工具已演练。

### 验收标准

- 1B+ synthetic entries，新增 shard 可扩容；
- 区域内详情 P95 < 300 ms；
- 核心写入可用性 99.99%；
- RPO <= 1 min，RTO <= 15 min；
- 单区域故障有明确切换或离线降级；
- 不执行跨区域每请求强一致 join；
- 原始地点证据和凭据永不进入云端。

## 9. 风险升级规则

- `CRITICAL`：立即停止阶段实施，由 Accountable DRI 决策；
- `HIGH`：24 小时内给出缓解方案，未解决不得过 Gate；
- `MEDIUM`：纳入本周迭代；
- `LOW`：记录并在阶段末复核。

连续三次被同一外部条件阻塞时，将阶段标记为 `BLOCKED`，明确所需的人类决策
或外部状态，不以假实现绕过。

## 10. 当前状态

截至 2026-09-17：

- 阶段 1：Host 范围 `COMPLETED`；
- 阶段 2：`VERIFICATION_DEFERRED`，实现和已有证据保留，剩余真机验证未完成；
- 阶段 3：`IN_PROGRESS`，负责人允许带验证欠项开始开发；
- 阶段 4–6：`BLOCKED`，继续等待 P0 gate 和容量条件。

main 合并/发布不代表固件发布验收。原阶段 2 工作项中的未勾选项继续有效。
