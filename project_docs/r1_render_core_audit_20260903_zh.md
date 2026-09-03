# R1 Render Core 审查记录（2026-09-03）

> 范围：基于 `origin/master` 的独立审查工作树 `codex/r1-review`。
> 本记录不代表主工作区未提交修改已经合入主线。

## 已完成验证

在没有 App Runtime、JerryScript、桌面壳或 ports 的 Render Core-only 配置中，分别完成：

| 配置 | 结果 |
| --- | --- |
| Debug | 构建通过，CTest 9/9 通过 |
| Release | 构建通过，CTest 9/9 通过 |
| AddressSanitizer/UndefinedBehaviorSanitizer | 构建通过，CTest 9/9 通过 |

覆盖内容包括 Core 单元回归、feature profile、feature registry、build boundary、
generated link map、脚本边界静态检查以及 HTML/CSS 能力表一致性。

## 当前结论

1. `origin/master` 的 Core-only 构建边界在三种配置下可复现，未发现新的构建或
   sanitizer 回归。
2. 既有安全审查中的资源尺寸、脚本 watchdog、Provider 输出限流、路径保护和
   JFDP 计数校验已有对应实现或定向回归；本轮没有证据表明需要重复修改。
3. `analyze_display_invalidation()`、`rasterize_clipped()` 等公共数组接口仍由调用方
   提供计数。当前协议调用方有 profile budget，但 Core 公共 API 没有统一的显式上限
   与超限结果。这应进入 API/RFC 设计，不应通过任意硬编码截断来掩盖。
4. `wrap_text_anywhere()` 的候选字符串测量具有最坏 O(n²) 成本。它用于保持字体
   run padding 与实际后端测量一致；在建立长文本基准和等价测量契约前，不改变算法。

## Benchmark 基线

在 Release、Render Core-only 构建中运行 `jellyframe_render_core_microbench`，当前
基线的 `wrap_text_anywhere()` 结果如下。单位为每次调用的平均微秒数：

| 输入长度 | 窄栏 96px | 宽栏 65536px |
| ---: | ---: | ---: |
| 32 | 0.44 | 0.35 |
| 128 | 1.68 | 1.40 |
| 512 | 6.18 | 5.15 |
| 2048 | 22.45 | 19.80 |

这组结果来自 `origin/master` 的基线实现，增长仍接近线性；它证明了当前已合入
路径在这组输入中没有明显的二次增长，但不代表未提交的候选实现。候选实现必须
使用相同编译器、配置、输入长度和重复次数重新采集，并同时验证每行内容和行数
完全一致，才可据此决定是否优化。

## 下一项工作

- 为 dirty、clip、display command 的公共入口提出统一的 `CoreBudget`/结果语义草案；
  先列出现有 Runtime、桌面和设备调用方的实际上限，再决定是否需要源码级 API 变化。
- 为文本换行补充短、中、长 UTF-8 文本及多种字体回调的 focused benchmark；只有
  在证实其影响真实 workload 后才优化测量路径。
- 保持 Core 独立仓库的 package、source override、profile 和 provenance CI 作为
  后续版本升级的必需门禁。
