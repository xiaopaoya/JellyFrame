# JFDP/1 Physical Transport Acceptance

> Last updated: 2026-08-18; Applies to: `JFDP/1`, JellyFrame `0.6.0-dev`

This is the port-side acceptance contract for the first real JellyFrame Device Protocol transport. It applies to USB CDC, USB Serial/JTAG, UART, Wi-Fi or a host bridge. It does not implement a transport, grant raw device access or turn the desktop reference endpoint into hardware evidence.

Read [device_runtime.md](device_runtime.md) first. The protocol and typed payload codecs are owned by `src/device_runtime_contracts`; a port supplies the byte stream, bounded receive state, task handoff and Device OS adapters.

## Scope And Non-Goals

The first endpoint carries only bounded `JFDP/1` control messages. It must not expose raw flash, arbitrary filesystem paths, native command execution or a general serial console through this channel. Physical firmware, transport drivers, storage and launcher policy remain port/Device OS work.

This acceptance proves byte compatibility and failure containment. It does not prove panel rendering, touch, package-signing policy, performance or a complete developer-image lifecycle. Those need their own A1/A2 evidence.

## Normative Wire Inputs

`tests/fixtures/jfdp_v1_wire_vectors.txt` is the canonical fixture. Its SHA-256 and repository commit must be recorded in every port report. The fixture is checked by two independent encoders:

- C++ `jellyframe_device_runtime_contracts_tests`;
- Python `tests/tool_regression/device_reference_cli_tests.py`.

The physical endpoint must send and receive these complete frames byte-for-byte:

| Vector | Direction | Required observation |
| --- | --- | --- |
| `frame-discovery` | host to device | Device accepts a discovery request with the supplied session/request ids. |
| `frame-capabilities-response` | device to host | Response retains discovery type and ids and sets response flag bit 0. |
| `frame-install-begin` | host to device | Device reaches the bounded begin decoder before touching storage. |
| `frame-install-chunk` | host to device | Device receives the declared transaction, offset and four bytes exactly. |
| `frame-install-commit-response` | device to host | Response retains commit type and ids and carries the typed operation result. |

The payload-only vectors in the same fixture are codec fixtures, not optional alternatives. A port must use the public `device_runtime_protocol.h` codec, or an independently tested byte-identical implementation; it must not use a port-private JSON or struct-memory encoding.

## Stream Adapter Contract

`decode_device_frame()` intentionally accepts one complete frame. A physical adapter therefore owns stream reassembly and must satisfy all of the following:

1. Accumulate exactly 24 header bytes before reading length or allocating a payload buffer. Integers are little endian.
2. Reject a declared payload above 4096 bytes before allocation, queueing or storage access.
3. Accumulate exactly the declared payload length. Dispatch only when the complete frame has passed magic, version, type, size and CRC validation.
4. Treat one read as arbitrary: it may return one byte, a header suffix, a complete frame, or multiple concatenated frames. Read boundaries are never message boundaries.
5. Copy decoded `DeviceInstallChunkView.bytes` before it crosses a task, queue, DMA lifetime or asynchronous storage boundary. No `payload`, `Node*`, `LayerNode*`, `jerry_value_t` or adapter buffer pointer may escape the receive task.
6. Bound receive buffers, outstanding requests and queue depth. A stalled host cannot make the endpoint retain unbounded bytes or work.
7. On a malformed header, over-limit length, CRC failure or mid-frame disconnect, discard the incomplete frame without storage/lifecycle side effects. The first endpoint may close and require reconnect rather than attempt in-stream magic resynchronisation; the chosen behavior must be deterministic and reported.

Bit 0 of `DeviceFrameHeader.flags` is `response`. A response preserves the request message type, session id and request id. Reserved flag bits have no meaning in `JFDP/1`; an endpoint may reject them but must not silently assign a private semantic. Request-correlation state belongs to the port/session owner, not to Render Core or the app task.

## Required Automated Cases

Run these on target firmware using a host probe that records sent and received bytes. The probe can be port-local; do not add a speculative transport to the platform-independent Runtime merely to run this checklist.

| Case | Procedure | Pass condition |
| --- | --- | --- |
| Exact vectors | Exchange every full-frame vector in the table. | Captured bytes exactly equal the fixture, including CRC and header. |
| Header fragmentation | Feed each required request one byte at a time and at every header boundary (1, 4, 5, 6, 8, 12, 16, 20, 24). | No dispatch before the final byte; final decode and response equal the full-frame case. |
| Payload fragmentation | Split each non-empty required request at every payload byte boundary. | Exactly one dispatch after the final byte; no lost or duplicated chunk. |
| Coalescing | Deliver two valid frames in one read, including discovery followed by install begin. | Two ordered dispatches and correctly correlated responses. |
| Bad CRC | Flip one payload byte in `frame-install-chunk` without changing the header. | No storage write, no transaction progress and deterministic disconnect/error behavior. |
| Oversize length | Send a syntactically valid header declaring 4097 bytes, then no payload. | Rejected before a 4097-byte allocation, queue entry or watchdog delay. |
| Invalid header | Independently corrupt magic, version and message type. | No handler/storage action; adapter returns to its documented reconnect/resync state. |
| Truncation | Disconnect after 23 header bytes and separately after a partial install chunk. Reconnect and send discovery. | No partial mutation; new session discovers normally. |
| Correlation | Issue at least two distinct request ids in one session and capture normal responses. | Each response has matching type/session/request id and response flag. |
| Repetition | Repeat connect, discovery and a valid no-op/control exchange 100 times. | No reset, panic, watchdog, receive-buffer leak or divergent capability response. |

Use the canonical install vectors only against an isolated staging fixture or transaction that is explicitly cleaned up. They do not by themselves represent a valid production `.jfapp` bundle and must not accidentally publish an app.

## Report And Acceptance Boundary

The port report must include:

- JellyFrame commit, Device OS image version, board/profile id and transport driver/configuration;
- Core package version/ABI/source identity and the exact vector-fixture SHA-256;
- host probe version, endpoint identifier and transport settings;
- byte captures or deterministic hashes for all five full-frame vectors;
- counts for every required case, reconnects, rejected frames, dispatches, storage writes and registry publications;
- heap/queue high-water marks where the platform exposes them, plus all reset, watchdog, transport and storage errors;
- a clear statement that this is **wire acceptance only**, or links to the separate developer-image lifecycle report when that broader work is tested.

The transport passes this gate only when every required case succeeds and no invalid frame causes an install mutation, registry publication or system reset. A passing wire report does not mark A1, A2 or external developer trial ready.
