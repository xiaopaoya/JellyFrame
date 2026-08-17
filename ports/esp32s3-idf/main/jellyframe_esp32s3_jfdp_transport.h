#pragma once

#include "device_runtime_contracts/device_runtime_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3 {

struct JfdpTransportCounters {
    std::uint32_t reads = 0;
    std::uint32_t received_bytes = 0;
    std::uint32_t received_high_water_bytes = 0;
    std::uint32_t dispatched_frames = 0;
    std::uint32_t rejected_frames = 0;
    std::uint32_t timed_out_frames = 0;
    std::uint32_t disconnects = 0;
    std::uint32_t responses = 0;
    std::uint32_t response_write_failures = 0;
    std::uint32_t staging_begins = 0;
    std::uint32_t staging_writes = 0;
    std::uint32_t staging_aborts = 0;
    std::uint32_t registry_publications = 0;
};

class JfdpStreamSink {
public:
    virtual ~JfdpStreamSink() = default;
    virtual void on_jfdp_frame(const std::uint8_t* frame, std::size_t frame_size) = 0;
    virtual void on_jfdp_transport_reset() = 0;
};

// Reassembles a byte stream into one bounded JFDP/1 frame at a time. The sink
// must consume synchronously; frame bytes remain owned by this adapter.
class JfdpStreamAdapter {
public:
    explicit JfdpStreamAdapter(JfdpStreamSink& sink);

    void feed(const std::uint8_t* bytes, std::size_t size, JfdpTransportCounters& counters);
    void reset(JfdpTransportCounters& counters, bool timed_out_or_disconnect);
    bool has_partial_frame() const;

private:
    void reject(JfdpTransportCounters& counters);
    void complete_frame(JfdpTransportCounters& counters);

    JfdpStreamSink& sink_;
    std::array<std::uint8_t,
               jellyframe::kDeviceProtocolHeaderBytes + jellyframe::kDeviceProtocolMaxPayloadBytes> buffer_{};
    std::size_t buffered_bytes_ = 0;
    std::size_t expected_frame_bytes_ = 0;
};

bool start_jfdp_transport_acceptance_task();

} // namespace jellyframe_esp32s3
