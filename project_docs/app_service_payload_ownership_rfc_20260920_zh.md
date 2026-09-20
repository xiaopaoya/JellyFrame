# App Service 载荷所有权与零拷贝 RFC

> 最后更新：2026-09-20；适用版本：0.6.0-dev
>
> 状态：Deferred RFC；不改变当前 `0.6.0-dev` API。
>
> 适用范围：`AppVideoFrameProviderMock` 的解码帧像素，以及后续可能出现的
> image/decode surface、compute result 等较大 service payload。

## 背景

当前视频帧 provider 同时保存可复用的 `AppVideoFrameFixture` 和已经交付给 App
的 `AppVideoFrameRecord`。完成请求时，fixture 的像素会复制到 record，使 record
在 fixture 被清理、替换或重排后仍然有效。这个复制是可观察的内存成本，但不是
可以直接删除的冗余：

- 只保存 fixture index 会在清理或重排后产生失效索引；
- 只把 fixture 像素移动到 record 会破坏同一 source 的下一帧复用；
- 只保存裸指针会把 App 生命周期、异步 completion 和 release 的所有权交给隐含约定；
- 直接把公开 `std::vector<uint8_t> pixels` 改成共享指针会破坏现有 consumer surface。

因此当前实现保留复制，并将该问题标记为 API 设计项，而不是未经证据的热路径修复。

## 候选方向

后续若真实设备 workload 证明像素复制是帧预算或内存门槛，应优先设计版本化的
只读 surface payload：

1. payload 由明确的 host-owned storage 持有，record 只保存带引用计数的 ownership token、
   字节数、stride、格式和几何元数据；
2. consumer 通过受限的 read-only view 访问像素，不能取得可写裸指针，也不能跨 session
   或 app instance 使用 token；
3. release、替换、clear、stale completion 和 app teardown 都通过同一 ownership table 回收；
4. quota 只按实际 storage 计数一次，多个 record 共享 payload 时不能重复计费或绕过预算；
5. 跨 task 传输只允许 value-only token/metadata，不能把 `std::vector`、DOM、JerryScript
   value 或 port-private pointer 带过边界。

实现形式可以是内部 `shared_ptr<const Payload>`，也可以是 host handle + bounded view，
但在公开 API 选择前不得把任一形式写入当前 `0.6` contract。shared ownership 只解决
生命周期，不自动解决跨任务 session 校验、预算和 device transport 语义。

## 必须先完成的证据

- 同一 source 连续提交至少 100 帧，确认 payload storage 不随 fixture 数量线性泄漏；
- fixture 清理、fixture 重排、replacement allocation failure 和 stale completion 后，
  已展示帧仍可读且不会复活旧 App 的 frame；
- release、collect、app teardown、重复 clear 的 storage/handle 计数回到基线；
- 单 payload、总 payload、handle budget 拒绝路径均保持现有错误分类；
- Debug、Release、ASan/UBSan 及 ESP32-S3 value-frame profile 均验证 token/session 边界；
- 真实 workload 证明复制成本高于 API 引入的引用表、校验和生命周期管理成本。

在上述证据和独立 API review 之前，`AppVideoFrameRecord::pixels` 的复制保持不变，
不新增硬件 A/B，也不更新 Runtime/Core ABI 或 SDK。
