# test/ —— PC 端单元测试

本目录存放**在开发机上运行**的单元测试（不需要开发板、不需要 ARM 工具链）。

```bash
make test          # 从仓库根目录执行：架构校验 → 编译 → 运行 → 汇总
```

## 文件说明

| 文件 | 作用 |
|---|---|
| `framework.h` | 约 100 行的自写断言框架（零第三方依赖） |
| `host_test_main.c` | 框架实现 + `main()`，依次调用 5 个套件并汇总结果 |
| `suites.h` | 5 个套件的入口声明 |
| `test_filter.c` | `Util/filter.c` 滑动平均 —— 8 个用例 |
| `test_fixed_str.c` | `Util/fixed_str.c` 定点格式化 —— 9 个用例 |
| `test_dht11_frame.c` | `Drivers/dht11_frame.c` 帧解码 —— 9 个用例 |
| `test_hist_index.c` | `Drivers/hist_index.c` 记录寻址 —— 8 个用例 |
| `test_esp_parse.c` | `Drivers/esp_parse.c` AT 应答匹配 —— 11 个用例 |

**合计 45 个用例 / 137 条断言。**

## 被测文件

```
Util/filter.c  Util/fixed_str.c
Drivers/dht11_frame.c  Drivers/hist_index.c  Drivers/esp_parse.c
```

这 5 个文件**不 include 任何 `bsp_*.h`**，因此能在 PC 上直接编译。
编译选项由 `Makefile` 提供，**只给 `-IUtil -IDrivers`，不给 `-IBSP`** ——
万一被测代码偷偷依赖了硬件头，这里会立刻编译失败（用构建配置守住分层边界）。

## 约定

- **输出用英文**：Windows 控制台默认 GBK 代码页，UTF-8 中文会乱码；CI 是 UTF-8。英文两边都正常。
- **失败的定位格式**：`文件:行号` + 期望值 / 实际值，只报告**第一条**失败（避免后续失败淹没根因）。
- **退出码**：全部通过返回 0，有任何失败返回 1（CI 依赖这个）。

## 相关文档

- **测试计划与报告**（用例清单、未覆盖部分、有效性验证、残余风险）：
  [`../docs/design/04-测试计划与报告.md`](../docs/design/04-测试计划与报告.md)
- 为什么这些模块能被测（可测性设计）：
  [`../docs/design/02-软件架构设计.md`](../docs/design/02-软件架构设计.md) §7
