#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分层架构校验（在 `make` 之前自动执行）

三条规则来自《岗位适配升级方案》2.1 节的硬性约束：

  规则 1  依赖单向     Core -> Drivers -> BSP -> Util，禁止反向
  规则 2  寄存器隔离   所有寄存器访问只允许出现在 BSP/ 层
  规则 3  接口收敛     BSP/Util/Drivers 的内部状态必须 static，
                       跨模块只能通过 .h 里的访问函数传递

规则 2 是整套方案的关键 —— 它保证了上层代码将来能在 PC 上编译并做单元测试。
规则 3 保证「谁改了状态」永远只有一处 —— 出问题时排查面从"全工程"缩到"一个文件"。
（Core/ 是顶层，它对外发布的共享状态就是应用的公共数据模型，允许在
  app.h / ui_pages.h 中直接声明，这是有意的设计，不算违规。）

用脚本把三条约束变成「构建时可验证」的规则，而不是靠人自觉遵守。

用法：python tools/check_layering.py      # 有空则退出码 1
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYERS = ("BSP", "Util", "Drivers", "Core")

# 允许的依赖方向：A 可以 include B 的头（A -> B）
ALLOWED = {
    "Core":    {"Core", "Drivers", "BSP", "Util"},
    "Drivers": {"Drivers", "BSP", "Util"},
    "BSP":     {"BSP", "Util"},
    "Util":    {"Util"},
}

# 规则 3 的检查范围：Core 作为顶层不查（见文件头说明）
SEALED = ("BSP", "Util", "Drivers")

# STM32 外设寄存器符号（出现在 BSP 以外的任何 .c/.h 都算违规）
REG_PATTERNS = [
    r"\bRCC_(?:CR|CFGR|APB1ENR|APB2ENR|CSR)\b",
    r"\bFLASH_ACR\b",
    r"\bGPIO[A-C]_(?:CRL|CRH|IDR|ODR|BSRR)\b",
    r"\bUSART[12]_(?:SR|DR|BRR|CR1)\b",
    r"\bSPI1_(?:CR1|SR|DR)\b",
    r"\bTIM2_(?:CR1|DIER|SR|EGR|CNT|PSC|ARR)\b",
    r"\bSYSTICK_(?:CTRL|LOAD|VAL)\b",
    r"\bNVIC_ISER[01]\b",
]
REG_RE = re.compile("|".join(REG_PATTERNS))

# 规则 3：顶层「非 static 变量定义」。不匹配函数定义（名字后面跟 '('）。
# const 限定的只读数据（常量字符串、查找表）不算违规 —— 规则要防的是
# 「多个模块都能改同一份状态」，而 const 根本没有写入点。
TYPE_RE = (r"(?:u?int(?:8|16|32)_t|int|char|short|long|float|double|size_t|"
           r"[A-Z]\w*_T|Filter_T)")
GLOBAL_DEF_RE = re.compile(
    r"^(?!static\b)(?!.*\bconst\b)(?:volatile\s+)?(?:unsigned\s+|signed\s+)?"
    r"%(t)s\s*\**\s*(\w+)\s*(?:\[[^\]]*\])?\s*(?:=|;)" % {"t": TYPE_RE}
)

# 规则 3：头文件里用 extern 暴露**可变**变量（而非函数原型）
EXTERN_VAR_RE = re.compile(
    r"^\s*extern\s+(?!.*\bconst\b)(?:volatile\s+)?(?:unsigned\s+|signed\s+)?"
    r"%(t)s\s*\**\s*(\w+)\s*(?:\[[^\]]*\])?\s*;" % {"t": TYPE_RE}
)


def owner_of(path):
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    return rel.split("/")[0] if "/" in rel else None


def header_layer(inc):
    """从 #include "xxx.h" 推断它属于哪一层"""
    name = inc.strip().strip('"').strip("<>")
    base = os.path.basename(name)
    for layer in LAYERS:
        if os.path.exists(os.path.join(ROOT, layer, base)):
            return layer
    return None


def main():
    errors, checked = [], 0

    for layer in LAYERS:
        d = os.path.join(ROOT, layer)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(d, fn)
            rel = layer + "/" + fn
            checked += 1
            text = open(path, encoding="utf-8", errors="replace").read()

            # ---- 规则 2：寄存器隔离 ----
            if layer != "BSP":
                for i, line in enumerate(text.splitlines(), 1):
                    code = line.split("/*")[0].split("//")[0]
                    m = REG_RE.search(code)
                    if m:
                        errors.append(
                            "%s:%d  规则2(寄存器隔离)  非 BSP 层出现寄存器 %s\n      %s"
                            % (rel, i, m.group(1), line.strip()[:90]))

            # ---- 规则 1：依赖单向 ----
            for inc in re.findall(r'#include\s+"([^"]+)"', text):
                tgt = header_layer(inc)
                if tgt and tgt not in ALLOWED[layer]:
                    errors.append(
                        "%s  规则1(依赖单向)  %s 层 include 了 %s 层的 %s"
                        % (rel, layer, tgt, inc))

            # ---- 规则 3：接口收敛 ----
            if layer in SEALED:
                for i, line in enumerate(text.splitlines(), 1):
                    code = line.split("/*")[0].split("//")[0]
                    if fn.endswith(".c"):
                        m = GLOBAL_DEF_RE.match(code)
                        if m:
                            errors.append(
                                "%s:%d  规则3(接口收敛)  %s 层出现非 static 全局变量 %s，"
                                "应改为 static + 访问函数\n      %s"
                                % (rel, i, layer, m.group(1), line.strip()[:88]))
                    else:
                        m = EXTERN_VAR_RE.match(code)
                        if m:
                            errors.append(
                                "%s:%d  规则3(接口收敛)  %s 层头文件用 extern 暴露变量 %s，"
                                "应改为访问函数原型\n      %s"
                                % (rel, i, layer, m.group(1), line.strip()[:88]))

    print("=" * 70)
    print("分层架构校验   共检查 %d 个文件" % checked)
    print("=" * 70)
    if errors:
        for e in errors:
            print("  [违规] " + e)
        print()
        print("结论：发现 %d 处违规 ✘" % len(errors))
        return 1
    print("  规则1 依赖单向   Core -> Drivers -> BSP -> Util   ✔")
    print("  规则2 寄存器隔离 寄存器访问仅存在于 BSP/ 层        ✔")
    print("  规则3 接口收敛   BSP/Util/Drivers 内部状态全 static ✔")
    print()
    print("结论：架构约束全部满足 ✔")
    return 0


if __name__ == "__main__":
    sys.exit(main())
