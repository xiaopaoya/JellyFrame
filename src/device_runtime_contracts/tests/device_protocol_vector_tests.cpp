#include "device_runtime_contracts/device_runtime_protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef JELLYFRAME_DEVICE_PROTOCOL_VECTOR_FILE
#error "JFDP wire vector fixture path is required for device contract tests."
#endif

using namespace jellyframe;

namespace {

using WireVector = std::pair<std::string, std::vector<std::uint8_t>>;

std::uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    assert(value >= 'a' && value <= 'f');
    return static_cast<std::uint8_t>(10 + value - 'a');
}

std::vector<WireVector> load_wire_vectors() {
    std::ifstream input(JELLYFRAME_DEVICE_PROTOCOL_VECTOR_FILE);
    assert(input.is_open());

    std::vector<WireVector> vectors;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t separator = line.find('=');
        assert(separator != std::string::npos);
        const std::string name = line.substr(0, separator);
        const std::string hex = line.substr(separator + 1);
        assert(!name.empty() && !hex.empty() && (hex.size() % 2) == 0);

        std::vector<std::uint8_t> bytes;
        bytes.reserve(hex.size() / 2);
        for (std::size_t index = 0; index < hex.size(); index += 2) {
            bytes.push_back(static_cast<std::uint8_t>((hex_nibble(hex[index]) << 4) |
                                                      hex_nibble(hex[index + 1])));
        }
        vectors.emplace_back(name, std::move(bytes));
    }
    assert(!vectors.empty());
    return vectors;
}

const std::vector<std::uint8_t>& vector_bytes(const std::vector<WireVector>& vectors,
                                               const char* name) {
    const auto found = std::find_if(vectors.begin(), vectors.end(), [name](const WireVector& value) {
        return value.first == name;
    });
    assert(found != vectors.end());
    return found->second;
}

template <std::size_t Capacity>
void assert_encoded_equals(const std::array<std::uint8_t, Capacity>& encoded,
                           std::size_t encoded_size,
                           const std::vector<std::uint8_t>& expected) {
    assert(encoded_size == expected.size());
    assert(std::equal(encoded.begin(), encoded.begin() + encoded_size, expected.begin()));
}

void assert_frame_vector(const std::vector<WireVector>& vectors,
                         const char* name,
                         const DeviceFrameHeader& header,
                         const std::uint8_t* payload,
                         std::size_t payload_size) {
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + kDeviceProtocolMaxPayloadBytes> encoded{};
    std::size_t encoded_size = 0;
    assert(encode_device_frame(header,
                               payload,
                               payload_size,
                               encoded.data(),
                               encoded.size(),
                               encoded_size) == DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, name));
}

void copy_string(std::array<char, kDeviceMaxAppIdBytes + 1>& destination, const char* value) {
    const std::size_t length = std::strlen(value);
    assert(length < destination.size());
    std::memcpy(destination.data(), value, length + 1);
}

void canonical_wire_vectors_match_reference_contract() {
    const auto vectors = load_wire_vectors();
    std::array<std::uint8_t, 256> encoded{};
    std::size_t encoded_size = 0;

    DeviceFrameHeader frame;
    frame.type = DeviceMessageType::Discovery;
    frame.session_id = 0x01020304u;
    frame.request_id = 0x10203040u;
    assert(encode_device_frame(frame, nullptr, 0, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "frame-discovery"));

    DeviceCapabilitySnapshot capabilities;
    capabilities.max_bundle_bytes = 4u * 1024u * 1024u;
    std::memcpy(capabilities.board_id, "reference-no-device", 20);
    std::memcpy(capabilities.runtime_version, "0.6.0-dev", 10);
    assert(encode_device_capabilities(capabilities, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "capabilities-reference"));

    frame = {};
    frame.type = DeviceMessageType::Discovery;
    frame.flags = kDeviceFrameFlagResponse;
    frame.session_id = 0x01020304u;
    frame.request_id = 0x10203040u;
    assert_frame_vector(vectors, "frame-capabilities-response", frame, encoded.data(), encoded_size);

    DeviceInstallBeginPayload begin;
    begin.transaction_id = 0x11223344u;
    begin.bundle_bytes = 0x12345u;
    begin.bundle_crc32 = 0x89abcdefu;
    begin.allow_downgrade = true;
    copy_string(begin.app_id, "org.jellyframe.demo");
    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "install-begin"));

    frame = {};
    frame.type = DeviceMessageType::InstallBegin;
    frame.session_id = 0x0a0b0c0du;
    frame.request_id = 0x01020304u;
    assert_frame_vector(vectors, "frame-install-begin", frame, encoded.data(), encoded_size);

    const std::array<std::uint8_t, 4> chunk{{0x00, 0x7f, 0x80, 0xff}};
    assert(encode_device_install_chunk_payload(0x11223344u,
                                               0x20u,
                                               chunk.data(),
                                               chunk.size(),
                                               encoded.data(),
                                               encoded.size(),
                                               encoded_size) == DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "install-chunk"));

    frame = {};
    frame.type = DeviceMessageType::InstallChunk;
    frame.session_id = 0x0a0b0c0du;
    frame.request_id = 0x01020305u;
    assert_frame_vector(vectors, "frame-install-chunk", frame, encoded.data(), encoded_size);

    DeviceTransactionPayload transaction;
    transaction.transaction_id = 0x11223344u;
    assert(encode_device_transaction_payload(transaction, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "transaction"));

    DeviceAppIdPayload app_id;
    copy_string(app_id.app_id, "org.jellyframe.demo");
    assert(encode_device_app_id_payload(app_id, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "app-id"));

    DeviceLogsRequestPayload logs;
    copy_string(logs.app_id, "org.jellyframe.demo");
    logs.limit = 16;
    assert(encode_device_logs_request_payload(logs, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "logs"));

    DeviceOperationResultPayload result;
    result.result_code = DeviceRequestResultCode::Accepted;
    result.flags = DeviceOperationResultComplete;
    result.transaction_id = 0x11223344u;
    result.received_bytes = 0x100u;
    result.expected_bytes = 0x12345u;
    assert(encode_device_operation_result_payload(result, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert_encoded_equals(encoded, encoded_size, vector_bytes(vectors, "operation-result"));

    frame = {};
    frame.type = DeviceMessageType::InstallCommit;
    frame.flags = kDeviceFrameFlagResponse;
    frame.session_id = 0x0a0b0c0du;
    frame.request_id = 0x01020306u;
    assert_frame_vector(vectors, "frame-install-commit-response", frame, encoded.data(), encoded_size);
}

} // namespace

int main() {
    canonical_wire_vectors_match_reference_contract();
    return 0;
}
