# Installed frame10 device follow-up

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Physical acceptance of the combined PR28 Core and Runtime PR58 image exposed
three port integration errors. The worker used initialization without supervisor
resource validation. The UI consumed a raw frame instead of its accepted resource
envelope. The temporary pixel ceiling equaled one viewport, which canonical
transactional replay charges for staging before allocating Core offscreen work.

The port now initializes through its supervisor, consumes accepted frame10
envelopes and budgets staging plus one viewport of offscreen pixels. The legacy
frame path keeps its existing one-viewport ceiling. Compact initialization and
render statuses remain visible within the endpoint's two-record log response.

WS147 migration5 at Runtime40b46975 with Core source5af3a7f6.../148 has R3,
Node identity, CoreText, BMP and frame10 enabled. The complete installed fixture
uses text, an image, rounded geometry, a button and timed mutations. Generation192
reports initialization Accepted, published3/accepted3 frames, render status
Accepted and zero presentation failures. Source failures and successive controls
are retained under E:/Experiments/migration-target-acceptance-20261112.

The original app slot, partition table and persistent storage were backed up.
The candidate partition table matches the device. Firmware identity and flash
hash verification are retained. Human visual/touch observation, repeated lifecycle
and heap/stack/latency admission remain pending. Legacy and additional new Core
target compilation is running for the follow-up. Required CI remains pending.
