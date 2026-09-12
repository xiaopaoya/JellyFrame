# render_core 代码审查报告

审查对象：`jellyframe-render-core-0.6.1`（`build/r1-released-core-0.6.1-1010/jellyframe-render-core-0.6.1`）
审查范围：光栅化与绘制（paint/raster）、布局与文本（layout/text），以及两者共用的头文件与数据结构
审查维度：性能、实现正确性、可读性

所有代码均按善意代码处理，不涉及安全性审查。下述问题按严重程度排列，每条给出文件、行号、影响与具体改法。

---

## 一、严重（每帧热路径上数量级级别的浪费）

### 1. `stroke_rect` / `stroke_rect_clipped` 对整块矩形逐像素求两次覆盖率

文件：`src/software_renderer.cpp:568-648`

圆角描边的实现是遍历**整个包围矩形**的每个像素，并对每个像素同时求外圈和内圈的圆角覆盖率：

```cpp
for (int y = clipped.y; y < y_end; ++y) {
    for (int x = clipped.x; x < x_end; ++x) {
        const int outer_coverage = rounded_rect_coverage(outer, x, y);   // :595
        ...
        const int inner_coverage = empty_rect(inner) ? 0 : rounded_rect_coverage(inner_geometry, x, y);  // :599
```

`rounded_rect_coverage` 对落在角区的像素要跑 4×4 子像素圆判定（`raster_primitives.h:169-179`）。于是对一个 1000×1000 的圆角边框、描边宽度 1，代码要访问 100 万像素，而真正属于边框带的只有约 4000 个。**约 250 倍的无效工作**，且发生在每帧重绘路径上。

改法：不要按包围盒遍历。描边是四条边带与四个角区的并集，应当按行计算该行的描边区间（`【左带 x..x+stroke_width)】∪【右带】`），在行内只对区间端点附近的角区求覆盖率，中间段直接一次 `blend_color`/`fill`。这与 `fill_opaque_rounded_rect`（`:455-521`）里"先填十字中心、再单独处理四个角"的写法是同一套思路——那里已经做对了，描边路径应当复用同样的分解。

### 2. `measure_text_with_letter_spacing` 与文本换行按码点逐个调用测量回调

文件：`src/text_backend.cpp:153-190`、`192-237`、`239-312`

`measure_text_with_letter_spacing` 在 `letter_spacing != 0` 时，对**每个码点**构造一个 `std::string` 并调用一次 host 测量回调：

```cpp
for (std::size_t begin = 0; begin < text.size();) {
    ...
    const TextMetrics scalar = measure_text(provider,
                                            std::string(text.substr(begin, end - begin)),  // :169-173
```

回调签名是 `bool (*)(const std::string&, ...)`（`text_backend.h:15-19`），所以这个临时 `std::string` 无法靠小字符串优化回避——每个码点一次堆分配、一次间接调用。对一段 500 字符的文本就是 500 次分配加 500 次 host 调用。

更糟的是 `wrap_text_anywhere`（`:219-223`）在同一个循环里又对每个码点重复了同样的调用，`wrap_text_at_opportunities` 的 `append_token`（`:256-278`）还额外做了两次字符串整体拷贝（`std::string candidate = line;` 然后 `candidate += token`），使每个词的代价随行长度线性增长，整段文本是 O(n²) 的拷贝。

改法，按收益排序：

1. 给 provider 增加一个 `measure_range(const char* data, size_t len, ...)` 的 `string_view` 变体，避免逐码点分配。这是根治。
2. 无 letter_spacing 时不要走逐码点路径——目前 `letter_spacing == 0` 已经短路到整串测量（`:159-161`），但 `wrap_text_anywhere` 没有这个分支，它对每个码点都调一次，即使没有字距。补上等价短路即可拿到大部分收益。
3. `append_token` 里用"先测量候选宽度、再决定是否拷贝"的顺序，或直接在原缓冲上累加长度，避免每词两次全行拷贝。

### 3. `transformed_render_text` 在每个文本节点被重复调用，且换行结果被反复丢弃

文件：`src/layout.cpp:897`、`src/layer_tree.cpp:1134`、`src/text_layout_reuse.cpp:46`

同一个文本节点在一条管线里至少被归一化三次：布局期一次（`layout.cpp:897`）、生成显示列表时一次（`layer_tree.cpp:1134`）、增量布局复用校验时一次（`text_layout_reuse.cpp:46`）。每次都要 `normalized_render_text` → `collapse_render_whitespace` 分配一个新字符串，且 `preserves_dom_text_whitespace`（`text_normalization.cpp:63-70`）每次都沿父链向上走到根，**每个文本节点 O(深度)**——嵌套深时这一步就会明显。

而 `layout_text_box` 在 `:927-950` 调用 `wrap_text_anywhere` / `wrap_text_balanced` / `wrap_text_at_opportunities` **仅仅为了取 `.size()` 拿行数**，拿到行数后把整个 `std::vector<std::string>` 扔掉；随后 `layer_tree.cpp:398-421` 为了真正生成显示列表又把同样的换行完整跑一遍。段落越长，这份重复越贵。

改法：把归一化后的文本、测量结果与换行结果一起缓存在 `LayoutBox` 上（`LayoutBox` 已经是每节点的持久结构），`layer_tree` 与 `text_layout_reuse` 直接复用；`preserves_dom_text_whitespace` 的结果也应在节点构建时算一次并记录为标志位，而不是每次查询沿父链重走。

---

## 二、中等（实现问题与局部效率）

### 4. `stroke_rect` 在 `stroke_width` 为奇数时会看到半个像素的斜接

文件：`src/software_renderer.cpp:568-607`

`inner` 用 `rect.x + stroke_width` 定位（`:582-587`），而 `stroke_width = std::min(stroke_width, std::max(1, std::min(rect.width, rect.height) / 2))`（`:573`）。当 `stroke_width` 为奇数、`rect` 尺寸为偶数时，`outer` 与 `inner` 的像素中心不对齐，`max(0, outer_coverage - inner_coverage)` 会在四条边的内沿留下一像素宽的半透明缝。这不是纯粹的观感问题——在深色背景上的浅色边框会看见内沿变暗。

改法：内矩形的半径按 `expand_corner_radii(border_radius, -stroke_width)` 调整的同时，内矩形原点应改为 `rect.x + stroke_width` 的前提下，把覆盖率比较改成在**同一子像素网格**上做（即内圈也按外圈的中心偏移求值），或在计算 `stroke_width` 时统一取整到与 rect 同奇偶。

### 5. `composite_buffer_clipped` 没有不透明快路径

文件：`src/software_renderer.cpp:918-935`

```cpp
for (int y = 0; y < copy_rect.height; ++y) {
    for (int x = 0; x < copy_rect.width; ++x) {
        blend_pixel(target, copy_rect.x + x, copy_rect.y + y,
                    with_opacity(source.pixel(src_x + x, src_y + y), opacity));   // :929-932
```

文本与图片只要被裁剪过，就会先画进临时缓冲、再走这条路径合成回主缓冲。这里每个像素都做了三件可省的事：`contains` 边界检查、行列乘法算索引、以及 `opacity == 1.0F` 时逐像素乘 `with_opacity`。

值得注意的是，同一文件里 `composite_rounded_clip_surface` 的 `:228-253` 已经实现了正确的不透明快路径（连续不透明段用 `std::copy_n` 整段搬运，只在遇到半透明像素时回退到 `source_over`）。同一份优化应当提取成共用助手，让 `composite_buffer_clipped`、`composite_transformed_buffer` 都用上。

### 6. `normalize_dirty_rects` 的合并是 O(n³)

文件：`src/software_renderer.cpp:382-425`

`while (merged)` 外层循环里套两层下标扫描，每合并一对就 `normalized.erase(...)`——在 `std::vector` 中间删除，把后面所有元素前移。脏矩形数量到几百时（滚动加动画很容易到这个量级），这段会成为帧内可测量的一块。

改法：这是一维区间合并的二维版本，可以用扫描线 + 并查集处理，或至少把 `erase` 换成"标记失效 + 最后一次性紧凑化"，并给外层循环加一个"本轮无合并即退出"的正确出口（目前靠 `merged` 标志，逻辑对，但每轮都从头重扫）。此外 `record_rounded_clip_replay_candidate_pixels` 里的 `intersect_rect(command_rect, temporary_surface_rect)`（`:128`）在统计关闭时才被 `statistics == nullptr` 提前返回挡住，顺序是对的，可保持。

### 7. `composite_rounded_clip_surface` 每行都重扫全部圆角裁剪

文件：`src/software_renderer.cpp:201-204`

```cpp
const bool row_needs_coverage = std::any_of(
    rounded_clips.begin(), rounded_clips.end(), [y](const RasterRoundedRect& rounded) {
        return rounded_clip_affects_row(rounded, y);
    });
```

对每一行遍历全部裁剪。裁剪链长度通常是 1–3，所以绝对量不大，但 `rounded_clip_affects_row` 判断角区行时完全可以用预计算的"圆角行区间"来 O(1) 判断，避免每行的这层间接调用。优先级低于上面几条。

### 8. `apply_rounded_clip` 对所有像素调用两次 `pixel()`，包括满覆盖区域

文件：`src/software_renderer.cpp:937-951`

`surface.pixel(x, y)` 每次调用都带 `assert(contains(...))` 并重新计算行索引（`:1292-1304`），这里每像素调用两次（读一次、写一次）。而 `rounded_rect_coverage` 在非角区恒定返回 255，此时 `with_coverage` 是恒等变换——整块区域的读写可以跳过。按行计算角区区间、只在角区逐像素处理，能省下大部分调用。

### 9. `draw_text` 的内置字形回退路径逐像素调用 `fill_rect`

文件：`src/software_renderer.cpp:895-915`

```cpp
for (int pass = 0; pass < stroke_passes; ++pass) {
    fill_rect(target,
              Rect{cursor_x + col * scale + pass, baseline_y + row * scale, scale, scale},  // :908-910
              color);
}
```

每个亮点一次 `fill_rect`，而每次 `fill_rect` 都会重算 `target_rect`、`clipped_target_rect` 并调用 `prepare_rounded_rect`（`:523-545`）。一个字符最多 35 个点 × 2 pass = 70 次调用。这条路径只在 host 未提供文本 painter 时启用，属于回退，但既然它是 `has_non_ascii` 时唯一可用的路径，值得让它直接写 pixel。

### 10. `rasterize_with_opacity` 逐命令拷贝 `DisplayCommand`

文件：`src/software_renderer.cpp:1233-1239`

```cpp
const DisplayCommand& source = display_list[index];
DisplayCommand command = source;             // :1235
```

`DisplayCommand` 内含 `std::string text`（`geometry.h:185-204`），按值拷贝会连带分配字符串。图层透明时，整条显示列表的每个命令都付一次这个代价。改法：把 `color`/`color2` 作为参数传给 `rasterize`，避免整体拷贝；这也是让 `DisplayCommand` 变窄（把文本相关字段挪到侧表）的顺带收益。

### 11. `measure_text_with_letter_spacing` 的 `letter_spacing == 0` 分支仍在拷贝

文件：`src/text_backend.cpp:160`

```cpp
return measure_text(provider, std::string(text), font_size, font_weight, font_family_hash);
```

`text` 已经是 `std::string_view`，而 `measure_text` 收的是 `const std::string&`。无字距时（绝大多数文本）白白多一次分配。给 `measure_text` 加 `std::string_view` 重载即可。

### 12. `layout_box` 的 `shift_box` 递归会随嵌套放大

文件：`src/layout.cpp:635-641`、`820-834`、`994-996`

`shift_box` 对整棵子树递归平移。行内布局的 `finish_line`（`:994-996`）对居中/右对齐的每一行都调用它，相对定位（`apply_relative_position_offset`，`:175-194`）也对整棵子树调用。深度为 d 的嵌套结构下，整个布局是 O(n·d)。对当前 0.6 的目标规模可以接受，但值得在注释里标注这个上界，或改为"记录父级偏移、绘制时一次性累加"。

---

## 三、可读性与实现规范

### 13. 文本溢出的诊断详情会无条件构造

文件：`src/layout.cpp:52-69`、`912-926`

`text_overflow_detail` 用 `std::ostringstream` 拼装，并且其中 `dom_node_path(box.node)`（`:67`）会沿 DOM 树构建完整路径字符串——即便诊断最终被丢弃。它只在超宽条件成立时才被调用（`:911-912` 的条件是对的），但一旦成立，代价远超测量本身。建议改为惰性：诊断 sink 支持 lambda，或先判断是否有 sink 再构造详情。

### 14. `composite_transformed_buffer` 的双线性采样可提公因子

文件：`src/software_renderer.cpp:1035-1075`

每个目标像素做 4 次带 `std::min/std::max` 夹紧的 `source.pixel()` 调用和 3 次 `lerp_color_fixed`。夹紧的边界在整行内是常量，可以提到行外；`tx`/`ty` 的定点化也可在行内复用以减少浮点运算。功能正确，属于纯优化空间。

### 15. `blend_pixel` 的边界检查在已裁剪路径中冗余

文件：`install/include/render_core/raster_primitives.h:76-81`

`blend_pixel` 每次调用都做 `target.contains(x, y)`，随后 `pixel()` 再做一次断言与索引计算。这在 `fill_rect`、`stroke_rect`、`apply_rounded_clip` 等已经用 `clipped_target_rect` 裁剪过的循环里是重复劳动。建议增加一个不做边界检查的 `blend_pixel_unchecked`，供这些已证明在界内的循环使用，保留 `blend_pixel` 给边界不确定的调用点。这是覆盖面最广的一处小优化——它出现在几乎所有逐像素循环里。

### 16. `Style` 结构体偏大，影响缓存局部性

文件：`install/include/render_core/style.h:257-297+`

`Style` 目前是数十个字段的扁平聚合，且被按值嵌在 `LayoutBox` 与 `RenderObject` 中（`layout.cpp:701`、`:742` 都做 `box->style = ...` 整体赋值）。布局遍历期间每个节点都要搬运整份样式。建议区分"继承属性"与"非继承属性"，或把冷字段（`text_shadow`、`object_position` 等）挪到侧表，用索引引用。

### 17. `flex_grid_paint.cpp` 的 `ordered_flex_paint_children` 与 `layout.cpp` 的 `ordered_flex_children` 逻辑重复

文件：`src/flex_grid_paint.cpp:9-28` 与 `src/layout.cpp:310-331`

两份实现只差一个 `is_out_of_flow_positioned` 判断，排序键、`has_nonzero_order` 快速路径都一样。两份逻辑分别演化迟早会不一致。建议合并为一份，参数化是否过滤绝对定位。

### 18. 类型转换噪音

文件：`install/include/render_core/raster_primitives.h:119-160`

`rounded_rect_coverage_detail` 里通篇是 `static_cast<std::int64_t>(x)` 这类转换，因为 `RasterRoundedRect` 的 `left/top/right/bottom` 是 `int`（`:85-92`）。把该结构的四个字段直接声明为 `std::int64_t`，可以消掉这些转换，显著提升可读性，行为不变——`prepare_rounded_rect` 写入时本就已经是 `safe_edge` 的结果。这是本次审查中性价比最高的一处可读性改动。

---

## 四、做得好的地方（建议保持）

`scroll_blit.cpp` 的 `apply_vertical_scroll_blit`（`:86-122`）处理得很细致：方向判断后用 `memmove` 逐行搬移，且在一系列前置条件里把整数溢出都挡住了（`:97-102`），这是热路径里少见的严谨写法。

`modern_paint.cpp:235-315` 的软阴影把 y 轴的夹紧与几何解算提到行外，并明确注释了为什么循环体不这样写（"resolving the rounded-rect geometry and y-axis clamp once avoids repeating invariant work for every shadow pixel"）。行内注释解释"为什么"而不是"是什么"，这是整个代码库注释的标准，值得在其它逐像素循环里推广。

`composite_rounded_clip_surface` 中"整段不透明用 `std::copy_n`、遇半透明回退"的思路（`:228-253`）是全库最好的热路径技巧，应当被提取成公共助手复用（见第 5 条）。

`arena.h` 的 `MonotonicArena` 带 `rewind()` 复用块（`:34-35`），配合 `frame_scratch` 的设计方向是对的；`make_layout_box`（`layout.cpp:752-757`）同时支持 arena 与堆分配，接口干净。

---

## 五、建议的处理顺序

按"收益/改动量"排序，建议这样推进：

1. **第 15 条**（`blend_pixel` 无检查变体）——改动最小，覆盖所有逐像素循环。
2. **第 1 条**（描边按边带遍历）——单点收益最大，且 `fill_opaque_rounded_rect` 已有可照抄的分解范式。
3. **第 11、2 条**（文本测量免分配、无字距短路）——一次性解决测量路径的分配问题。
4. **第 5 条**（提取不透明合成助手）——把已有技巧推广，顺带修掉临时缓冲合成的浪费。
5. **第 3 条**（文本测量与换行结果缓存）——改动涉及 `LayoutBox` 结构，但省掉整条管线里两次冗余的归一化与换行。
6. 其余条目按第 4、6、8、10、13、14、16、17、18 的顺序处理。

第 4 条（奇数描边的半透明缝）属于实现正确性，建议与第 1 条一起修，因为改动的是同一段代码。
