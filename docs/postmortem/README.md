# 故障复盘集

本目录收录开发过程中遇到的**典型故障**，每篇按 **现象 → 定位过程 → 根因 → 修复与验证** 四段组织。

## 为什么要写这个

设计文档回答"它怎么工作"，复盘回答**"它怎么坏过、以及我是怎么找到原因的"**。

后者的价值往往更高：

- **定位过程**才是可复用的能力。一个故障的根因可以背下来，但"在信息不足时如何二分排除"只能靠案例积累
- **现象描述**能帮未来的自己（或接手的人）快速匹配——"我看到的这个怪异现象，是不是以前遇到过"
- 复盘天然包含**走过的弯路**，而设计文档只会写最终正确的路径

> 只有根因、没有定位过程的记录，等于只给了答案没给方法。

## 这些故障也都对应 GitHub Issue

本目录是**归档**（可随代码一起版本管理、可在本地检索）；GitHub Issues 是**过程留痕**（带时间、带状态、可被引用）。
两者内容一致，Issue 创建后即关闭（已经修复），形成"发现问题 → 记录 → 修复 → 验证 → 关闭"的完整闭环。

**8 个 Issue 已全部创建并关闭**（[Issues 列表](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues?q=is%3Aissue)）。
批量创建命令见 `tools/release_v1.2.0.md` 的 §3。

## 目录

| # | 故障 | Issue | 现象一句话概括 | 类别 | 价值 |
|---|---|---|---|---|---|
| 01 | [串口 115200 丢字节](01-串口丢字节.md) | [#1](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/1) | 每条 AT 指令都超时，回显里只剩下一个 `\r` | 通信 | ★★★ 一个字节的证据定案 |
| 02 | [W25Q64 读回 0x000000 / 0xFFFFFF](02-W25Q64-DO-DI接反.md) | [#2](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/2) | Flash 识别失败，读 ID 得到全 0 或全 1 | 接线 | ★★ 症状与原因的映射 |
| 03 | [蜂鸣器不响 / 一直长鸣](03-蜂鸣器不响与长鸣.md) | [#3](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/3) | 要么完全没声，要么上电就一直响 | 硬件形态 | ★★★ "先确认形态再写代码" |
| 04 | [OLED 历史画面文字错乱](04-OLED历史画面错位.md) | [#4](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/4) | 历史页糊成一团，第 4 条一个字都不显示 | 显示 | ★★★ 静默失败最难查 |
| 05 | [历史画面进去出不来](05-历史画面单向门.md) | [#5](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/5) | 短按只翻页，用户被困在历史画面 | 交互设计 | ★★★ 设计要求而非 bug |
| 06 | [ESP-01S 所有 AT 指令超时](06-ESP01S全部AT超时.md) | [#6](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/6) | 模块毫无反应，或永远回 `link is not valid` | 供电/固件 | ★★★ 三个独立原因叠加 |
| 07 | [极值画面恒显示 3276.7 / −3276.8](07-极值画面恒显示极端值.md) | [#7](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/7) | 极值画面一直显示 int16 的两个极端值 | 逻辑缺陷 | ★★★★ 最隐蔽：看起来像正常数字 |
| 08 | [烧录后串口一条数据都没有](08-烧录后串口无数据.md) | [#8](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/issues/8) | 烧录成功，但串口收 0 字节 | 工具链 | ★★★ 工具行为的坑 |

## 从这些故障里总结出的通用教训

| # | 教训 | 对应案例 |
|---|---|---|
| 1 | **"看起来像正常值"的错误最危险**。全 0xFF 一眼就知道是错的；3276.7 看起来只是个偏大的读数 | 07 |
| 2 | **静默失败必须靠约束兜住**。`page = 7` 不画任何东西、不报错；所以要用小数字常量和断言把它锁住 | 04 |
| 3 | **先确认硬件形态，再写代码**。同一个"蜂鸣器"有 2 针/3 针、有源/无源、高触发/低触发多种形态 | 03 |
| 4 | **留一个字节的证据就够定案**。回显里那个被转成空格的 `\r`，比任何猜测都有力 | 01 |
| 5 | **症状与常见原因的对应表能省几小时**。`0x000000`/`0xFFFFFF` ⇒ 先怀疑 DO/DI 接反 | 02 |
| 6 | **要求实现的功能，必须同时设计它的"退出路径"**。只能进不能出的画面是设计缺陷，不是 bug | 05 |
| 7 | **同一个故障可能由多个独立原因叠加**。不要找到第一个原因就停下 | 06 |
| 8 | **工具的成功提示不等于结果可用**。"烧录成功"和"程序在跑"是两件事 | 08 |
