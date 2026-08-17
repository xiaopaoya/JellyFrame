#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

// JFAPPV0 is an installable resource container. This contract deliberately
// owns byte validation and bounded lookup only; storage policy and app startup
// remain Device OS and App Runtime responsibilities.
constexpr std::size_t kDeviceBundleHeaderBytes = 56;
constexpr std::size_t kDeviceBundleResourceEntryBytes = 28;
constexpr std::size_t kDeviceBundleMaxSummaryBytes = 4096;
constexpr std::size_t kDeviceBundleMaxResourcePathBytes = 255;
constexpr std::size_t kDeviceBundleMaxAppIdBytes = 95;
constexpr std::size_t kDeviceBundleMaxAppNameBytes = 95;
constexpr std::size_t kDeviceBundleMaxVersionNameBytes = 63;
constexpr std::size_t kDeviceBundleMaxEntryPathBytes = 191;
constexpr std::size_t kDeviceBundleMaxRuntimeVersionBytes = 31;

enum class DeviceBundleStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    ReadFailed,
    PayloadTooLarge,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidHeader,
    InvalidSection,
    TooManyResources,
    InvalidResource,
    BadChecksum,
    InvalidSummary,
    VersionRejected,
    ResourceNotFound,
    BufferTooSmall,
};

// Implemented by a storage adapter. A read must either copy exactly size bytes
// from the immutable bundle snapshot or fail; it never exposes storage-owned
// pointers beyond its own task/lifetime boundary.
class DeviceBundleReader {
public:
    virtual ~DeviceBundleReader() = default;

    virtual bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const = 0;
};

class DeviceBundleMemoryReader final : public DeviceBundleReader {
public:
    DeviceBundleMemoryReader(const std::uint8_t* bytes, std::size_t size) : bytes_(bytes), size_(size) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override;

private:
    const std::uint8_t* bytes_ = nullptr;
    std::size_t size_ = 0;
};

struct DeviceBundleValidationPolicy {
    // These are safe host defaults, not a promise that every target has this
    // much storage. A Device OS profile should override both values with its
    // partition and memory budgets instead of relying on an implicit zero.
    std::uint32_t max_bundle_bytes = 4u * 1024u * 1024u;
    std::uint32_t max_resource_entries = 4096;
    std::uint32_t max_summary_bytes = static_cast<std::uint32_t>(kDeviceBundleMaxSummaryBytes);
    std::string_view required_min_jellyframe;
    std::string_view required_min_render_core;
};

enum class DeviceBundleAppRole : std::uint8_t {
    App,
    Launcher,
    Watchface,
    Settings,
};

enum class DeviceBundleScriptMode : std::uint8_t {
    None,
    Classic,
};

struct DeviceBundleSummary {
    std::array<char, kDeviceBundleMaxAppIdBytes + 1> app_id{};
    std::array<char, kDeviceBundleMaxAppNameBytes + 1> app_name{};
    std::array<char, kDeviceBundleMaxVersionNameBytes + 1> version_name{};
    std::array<char, kDeviceBundleMaxEntryPathBytes + 1> entry_path{};
    std::array<char, kDeviceBundleMaxRuntimeVersionBytes + 1> min_jellyframe{};
    std::array<char, kDeviceBundleMaxRuntimeVersionBytes + 1> min_render_core{};
    std::uint32_t version_code = 0;
    DeviceBundleAppRole role = DeviceBundleAppRole::App;
    DeviceBundleScriptMode script_mode = DeviceBundleScriptMode::None;

    std::string_view app_id_view() const;
    std::string_view app_name_view() const;
    std::string_view version_name_view() const;
    std::string_view entry_path_view() const;
};

struct DeviceBundleDescriptor {
    DeviceBundleSummary summary;
    std::uint32_t bundle_bytes = 0;
    std::uint32_t bundle_crc32 = 0;
    std::uint32_t resource_count = 0;
    std::uint32_t summary_offset = 0;
    std::uint32_t summary_bytes = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t string_table_offset = 0;
    std::uint32_t string_table_bytes = 0;
    std::uint32_t payload_offset = 0;
    std::uint32_t payload_bytes = 0;
};

struct DeviceBundleResource {
    std::uint16_t kind = 0;
    std::uint32_t payload_offset = 0;
    std::uint32_t payload_bytes = 0;
    std::uint32_t payload_crc32 = 0;
};

DeviceBundleStatus inspect_device_bundle(const DeviceBundleReader& reader,
                                         std::uint32_t bundle_bytes,
                                         const DeviceBundleValidationPolicy& policy,
                                         DeviceBundleDescriptor& descriptor);

DeviceBundleStatus find_device_bundle_resource(const DeviceBundleReader& reader,
                                               const DeviceBundleDescriptor& descriptor,
                                               std::string_view app_path,
                                               DeviceBundleResource& resource);

DeviceBundleStatus read_device_bundle_resource(const DeviceBundleReader& reader,
                                               const DeviceBundleDescriptor& descriptor,
                                               const DeviceBundleResource& resource,
                                               std::uint8_t* output,
                                               std::size_t output_capacity,
                                               std::size_t& output_size);

const char* device_bundle_status_name(DeviceBundleStatus status);
const char* device_bundle_app_role_name(DeviceBundleAppRole role);
const char* device_bundle_script_mode_name(DeviceBundleScriptMode mode);

} // namespace jellyframe
