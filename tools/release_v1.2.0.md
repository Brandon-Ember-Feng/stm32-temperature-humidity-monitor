# 发布 v1.2.0 · 操作手册

> 事项 2.4「维护与迭代留痕」的执行步骤。
> 这三件事都需要**能访问 GitHub**（本机走代理 `127.0.0.1:7890`，先确认代理已启动）。

## 前置检查

```bash
# 1. 代理是否通
git ls-remote --heads origin

# 如果不通：先启动代理，或临时直连试试（通常会被 reset）
git -c http.proxy= -c https.proxy= ls-remote --heads origin
```

当前仓库的代理配置：

```bash
git config --get-regexp proxy
# http.proxy  http://127.0.0.1:7890
# https.proxy http://127.0.0.1:7890
```

---

## 1. 版本号统一：`v0.1.0` → `v1.2.0`

### 背景

| 项 | 现状 |
|---|---|
| 代码里的版本 | `v1.2`（`main.c` 注释、串口启动横幅、OLED 就绪画面） |
| git tag | `v0.1.0`（指向 `37fff0c`，附注 tag） |
| **问题** | 两个版本号不一致，查看者会困惑"到底哪个版本" |

### 目标状态

| 项 | 目标 |
|---|---|
| 代码里的功能里程碑 | `v1.2`（保持不变） |
| git tag / Release | **`v1.2.0`**（SemVer，minor 位对应功能里程碑） |
| 旧 tag | `v0.1.0` 删除（本地 + 远端） |

对应关系已在 `CHANGELOG.md` 的「版本号说明」一节写明。

### 执行

```bash
# ① 创建新 tag（附注 tag，指向当前 HEAD）
git tag -a v1.2.0 -m "v1.2.0 —— STM32F103C8T6 温湿度监测仪

固件功能 v1.2 + 工程化改造。

功能：DHT11 采集 / 5 点滑动平均 / 5 画面 OLED / 历史极值 /
      W25Q64 历史记录（双扇区乒乓，掉电不丢）/ 超阈值 LED + 蜂鸣器报警 /
      ESP-01S 自建热点 + TCP 推送 / 上电自检与故障定位 / 外设独立降级

工程化：四层架构（Core/Drivers/BSP/Util）+ 三条硬性约束自动强制 /
       命令行 Makefile 构建 / PC 端单元测试 45 用例 137 断言 / GitHub Actions CI /
       设计文档四件套 / 故障复盘集

资源占用：Flash 18.2 KB / 64 KB，RAM 1.9 KB / 20 KB，编译零警告"

# ② 推送新 tag
git push origin v1.2.0

# ③ 删除旧的远端 tag 与本地 tag
git push origin :refs/tags/v0.1.0
git tag -d v0.1.0
```

> ⚠️ 第 ③ 步会**删除远端已存在的 tag**。这是本仓库自己的一次版本号统一，
> 仓库为单人维护、旧 tag 无外部引用，因此安全。
> 若该 tag 已被别人引用过，应改为**保留旧 tag**、只新增 `v1.2.0`。

### 验证

```bash
git tag -l -n9            # 只剩 v1.2.0
git ls-remote --tags origin
```

---

## 2. GitHub Release

```bash
gh release create v1.2.0 \
  --title "v1.2.0 —— 温湿度监测仪（固件 v1.2 + 工程化改造）" \
  --notes-file CHANGELOG.md \
  --latest
```

若旧 Release 存在，先删掉或改名：

```bash
gh release list
gh release delete v0.1.0 --yes     # 如需删除旧 Release
```

**Release 正文建议**（可直接用 `CHANGELOG.md` 的 `[v1.2.0]` 小节，或摘出下面的简版）：

```markdown
## 这个版本包含什么

**固件功能 v1.2**：DHT11 采集 → 5 点滑动平均 → 5 画面 OLED 显示 →
W25Q64 掉电存储（双扇区乒乓）→ 超阈值 LED + 蜂鸣器报警 →
ESP-01S 自建 WiFi 热点 + TCP 服务器推送（手机直连，不依赖路由器）。

**工程化改造**（本版新增，不改变功能）：
- 四层架构重构：`Core / Drivers / BSP / Util`，寄存器隔离在 BSP 层
- 三条硬性架构约束，由脚本在编译前自动强制（违规即编译失败）
- 命令行 Makefile 构建，不依赖 IDE
- PC 端单元测试：45 用例 / 137 断言，`make test` 一条命令
- GitHub Actions CI：架构校验 → 交叉编译零警告 → 单元测试
- 设计文档四件套 + 8 篇故障复盘

## 资源占用

| 项 | 值 |
|---|---|
| Flash | 18.2 KB / 64 KB（28.5%） |
| RAM | 1.9 KB / 20 KB（9.5%） |
| 编译警告 | 0（`-Wall -Wextra`） |

## 已知限制

记录序号 uint16 回绕（≈1090 小时）｜湿度记录限幅 25.5%｜
掉电一致性未实测｜无硬件看门狗。详见 `CHANGELOG.md`。

## 硬件接线

见 `README.md` 第 3.2 节接线表。
```

---

## 3. 批量创建 Issues（把已修复的故障补成闭环）

8 篇故障复盘的正文在 `docs/postmortem/`。逐条创建并**立即关闭**（标注"已修复"），
形成"发现问题 → 记录 → 修复 → 验证 → 关闭"的完整闭环。

```bash
cd <仓库根目录>

# ---- 01 ----
gh issue create \
  --title "[Bug] 串口 115200 下接收丢字节，所有 AT 指令超时" \
  --label "bug,已修复" \
  --body-file docs/postmortem/01-串口丢字节.md

# ---- 02 ----
gh issue create \
  --title "[Bug] W25Q64 读回 0x000000 / 0xFFFFFF，识别失败" \
  --label "bug,接线,已修复" \
  --body-file docs/postmortem/02-W25Q64-DO-DI接反.md

# ---- 03 ----
gh issue create \
  --title "[Bug] 蜂鸣器不响 / 上电一直长鸣（3 针模块 + 触发极性）" \
  --label "bug,硬件形态,已修复" \
  --body-file docs/postmortem/03-蜂鸣器不响与长鸣.md

# ---- 04 ----
gh issue create \
  --title "[Bug] OLED 历史画面文字错乱，第 4 条整行不显示" \
  --label "bug,显示,已修复" \
  --body-file docs/postmortem/04-OLED历史画面错位.md

# ---- 05 ----
gh issue create \
  --title "[Bug] 历史画面进去出不来（单向门，缺逃生通道）" \
  --label "bug,交互设计,已修复" \
  --body-file docs/postmortem/05-历史画面单向门.md

# ---- 06 ----
gh issue create \
  --title "[Bug] ESP-01S 全部 AT 指令超时（CH_PD / 供电 / 僵尸链路）" \
  --label "bug,通信,供电,已修复" \
  --body-file docs/postmortem/06-ESP01S全部AT超时.md

# ---- 07 ----
gh issue create \
  --title "[Bug] 极值画面恒显示 3276.7 / -3276.8（功能实际未实现）" \
  --label "bug,已修复" \
  --body-file docs/postmortem/07-极值画面恒显示极端值.md

# ---- 08 ----
gh issue create \
  --title "[Bug] 烧录成功但串口 0 字节（芯片停在 halt）" \
  --label "bug,工具链,已修复" \
  --body-file docs/postmortem/08-烧录后串口无数据.md
```

### 创建标签（首次执行）

`--label` 用到的标签需要先存在，否则 `gh` 会报错：

```bash
gh label create "已修复"     --color "0E8A16" --description "问题已定位并修复，验证通过"
gh label create "接线"       --color "FBCA04" --description "硬件接线相关"
gh label create "硬件形态"   --color "FBCA04" --description "器件形态认知错误"
gh label create "供电"       --color "D93F0B" --description "电源相关"
gh label create "通信"       --color "1D76DB" --description "串口 / 总线 / 网络"
gh label create "显示"       --color "5319E7" --description "OLED 相关"
gh label create "交互设计"   --color "C2E0C6" --description "交互 / 状态机完整性"
gh label create "工具链"     --color "BFD4F2" --description "编译 / 烧录 / 构建"
```

### 关闭它们（标注已修复）

```bash
# 列出刚创建的 issue 编号
gh issue list --state open

# 逐个关闭并留一条说明（用编号替换 N）
gh issue close N --comment "已在 v1.2.0 中修复并验证。详细复盘见 docs/postmortem/。"
```

> **为什么要关闭而不是留着**：这些是**已解决问题**的记录，不是待办。
> 留着 open 会让 Issues 列表失去意义（看不出还有什么没做）。
> 通过 `#编号` 引用它们才是正确的后续用法。
> 真正未完成的事项在 `CHANGELOG.md` 的「未发布 / 计划中」一节。

---

## 4. 推送代码

```bash
git push origin main
```

### 验证清单

| 项 | 检查方式 |
|---|---|
| tag 只剩 v1.2.0 | `git ls-remote --tags origin` |
| Release 页面显示 v1.2.0 | 打开 Releases 页 |
| CI 变绿 | 打开 Actions 页，确认 badge 是 passing |
| README 上的 CI 徽章正常 | 打开仓库首页 |
| 8 个 Issue 已关闭 | `gh issue list --state closed` |
| Issue 模板生效 | 打开 New Issue，应看到两个模板 |

---

## 5. 完成后需要同步的地方

| 文件 | 需要更新什么 |
|---|---|
| `README.md` | 若在本次发布中改了任何使用方式（本版未改） |
| `C:\Users\K\Desktop\嵌入式\温湿度监测仪-岗位适配升级方案.md` | 勾掉事项 2.4 |
| 项目长期记忆 | 记录 v1.2.0 已发布、tag 已统一 |
