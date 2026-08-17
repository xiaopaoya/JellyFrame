#include "device_runtime_contracts/device_bundle.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

constexpr std::uint32_t kCrcInitial = 0xffffffffu;

void write_le16(std::uint8_t* bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void write_le32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t crc32_extend(std::uint32_t state, const std::uint8_t* bytes, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        state ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            state = (state >> 1u) ^ (0xedb88320u & (0u - (state & 1u)));
        }
    }
    return state;
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    return ~crc32_extend(kCrcInitial, bytes, size);
}

std::uint32_t fnv1a32(const std::string& value) {
    std::uint32_t hash = 0x811c9dc5u;
    for (const char ch : value) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 0x01000193u;
    }
    return hash;
}

std::vector<std::uint8_t> make_bundle(std::string summary) {
    const std::string path = "/index.html";
    const std::string payload = "<p>ok</p>";
    const std::uint32_t summary_offset = kDeviceBundleHeaderBytes;
    const std::uint32_t index_offset = summary_offset + static_cast<std::uint32_t>(summary.size());
    const std::uint32_t strings_offset = index_offset + kDeviceBundleResourceEntryBytes;
    const std::uint32_t payload_offset = strings_offset + static_cast<std::uint32_t>(path.size());
    std::vector<std::uint8_t> bundle(payload_offset + payload.size());

    std::memcpy(bundle.data(), "JFAPPV0\0", 8);
    write_le16(bundle.data() + 8, kDeviceBundleHeaderBytes);
    write_le16(bundle.data() + 10, 0);
    write_le32(bundle.data() + 12, 0);
    write_le32(bundle.data() + 16, summary_offset);
    write_le32(bundle.data() + 20, static_cast<std::uint32_t>(summary.size()));
    write_le32(bundle.data() + 24, index_offset);
    write_le32(bundle.data() + 28, 1);
    write_le32(bundle.data() + 32, strings_offset);
    write_le32(bundle.data() + 36, static_cast<std::uint32_t>(path.size()));
    write_le32(bundle.data() + 40, payload_offset);
    write_le32(bundle.data() + 44, static_cast<std::uint32_t>(payload.size()));
    write_le32(bundle.data() + 48, 0);
    write_le32(bundle.data() + 52, 0);
    std::memcpy(bundle.data() + summary_offset, summary.data(), summary.size());
    std::memcpy(bundle.data() + strings_offset, path.data(), path.size());
    std::memcpy(bundle.data() + payload_offset, payload.data(), payload.size());

    std::uint8_t* entry = bundle.data() + index_offset;
    write_le32(entry, fnv1a32(path));
    write_le32(entry + 4, 0);
    write_le16(entry + 8, static_cast<std::uint16_t>(path.size()));
    write_le16(entry + 10, 1);
    write_le32(entry + 12, 0);
    write_le32(entry + 16, static_cast<std::uint32_t>(payload.size()));
    write_le32(entry + 20, crc32(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
    write_le32(entry + 24, 0);

    write_le32(bundle.data() + 48, crc32(bundle.data(), bundle.size()));
    return bundle;
}

void refresh_bundle_crc(std::vector<std::uint8_t>& bundle) {
    write_le32(bundle.data() + 48, 0);
    write_le32(bundle.data() + 48, crc32(bundle.data(), bundle.size()));
}

std::string valid_summary() {
    return "{\"entry\":\"/index.html\",\"id\":\"org.example.device\",\"minJellyFrame\":\"0.6.0\","
           "\"minRenderCore\":\"0.6.0\",\"name\":\"Device App\",\"role\":\"app\",\"script\":\"none\","
           "\"versionCode\":7,\"versionName\":\"1.2.3\"}";
}

DeviceBundleValidationPolicy policy() {
    DeviceBundleValidationPolicy value;
    value.max_bundle_bytes = 1024 * 1024;
    value.max_resource_entries = 8;
    value.required_min_jellyframe = "0.6.0";
    value.required_min_render_core = "0.6.0";
    return value;
}

void validates_bundle_and_reads_resource() {
    const std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    DeviceBundleMemoryReader reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::Ok);
    assert(descriptor.summary.app_id_view() == "org.example.device");
    assert(descriptor.summary.version_name_view() == "1.2.3");
    assert(descriptor.summary.version_code == 7);
    assert(descriptor.resource_count == 1);

    DeviceBundleResource resource;
    assert(find_device_bundle_resource(reader, descriptor, "/index.html", resource) == DeviceBundleStatus::Ok);
    std::array<std::uint8_t, 32> output{};
    std::size_t output_size = 0;
    assert(read_device_bundle_resource(reader, descriptor, resource, output.data(), output.size(), output_size) ==
           DeviceBundleStatus::Ok);
    assert(std::string(reinterpret_cast<const char*>(output.data()), output_size) == "<p>ok</p>");
}

void default_policy_is_safe_and_usable() {
    const std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    DeviceBundleMemoryReader reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(reader,
                                 static_cast<std::uint32_t>(bytes.size()),
                                 DeviceBundleValidationPolicy{},
                                 descriptor) == DeviceBundleStatus::Ok);
}

void rejects_bad_bundle_or_resource_checksum() {
    std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    bytes.back() ^= 0x01u;
    DeviceBundleMemoryReader bad_bundle_reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(bad_bundle_reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::BadChecksum);

    bytes = make_bundle(valid_summary());
    const std::uint32_t index_offset = kDeviceBundleHeaderBytes + static_cast<std::uint32_t>(valid_summary().size());
    bytes[index_offset + 20] ^= 0x01u;
    refresh_bundle_crc(bytes);
    DeviceBundleMemoryReader bad_resource_reader(bytes.data(), bytes.size());
    assert(inspect_device_bundle(bad_resource_reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::BadChecksum);
}

void rejects_ambiguous_or_incompatible_summary() {
    std::string duplicate = valid_summary();
    duplicate.pop_back();
    duplicate += ",\"id\":\"org.example.other\"}";
    std::vector<std::uint8_t> bytes = make_bundle(duplicate);
    DeviceBundleMemoryReader duplicate_reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(duplicate_reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::InvalidSummary);

    std::string incompatible = valid_summary();
    const std::size_t version = incompatible.find("0.6.0");
    assert(version != std::string::npos);
    incompatible.replace(version, 5, "9.9.9");
    bytes = make_bundle(incompatible);
    DeviceBundleMemoryReader incompatible_reader(bytes.data(), bytes.size());
    assert(inspect_device_bundle(incompatible_reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::VersionRejected);

    std::string control_character = valid_summary();
    const std::size_t name = control_character.find("Device App");
    assert(name != std::string::npos);
    control_character.replace(name, std::strlen("Device App"), "Device\\nApp");
    bytes = make_bundle(control_character);
    DeviceBundleMemoryReader control_reader(bytes.data(), bytes.size());
    assert(inspect_device_bundle(control_reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::InvalidSummary);
}

class FailingReader final : public DeviceBundleReader {
public:
    explicit FailingReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        if (offset >= fail_at_ || offset > bytes_.size() || size > bytes_.size() - offset) {
            return false;
        }
        std::memcpy(output, bytes_.data() + offset, size);
        return true;
    }

    std::uint32_t fail_at_ = 56;

private:
    const std::vector<std::uint8_t>& bytes_;
};

void read_failures_never_become_valid_bundles() {
    const std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    FailingReader reader(bytes);
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::ReadFailed);
}

void resource_reads_stay_inside_the_declared_payload_section() {
    const std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    DeviceBundleMemoryReader reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), policy(), descriptor) ==
           DeviceBundleStatus::Ok);

    DeviceBundleResource forged;
    forged.payload_offset = descriptor.summary_offset;
    forged.payload_bytes = 1;
    std::array<std::uint8_t, 8> output{};
    std::size_t output_size = 0;
    assert(read_device_bundle_resource(reader, descriptor, forged, output.data(), output.size(), output_size) ==
           DeviceBundleStatus::BufferTooSmall);
    assert(output_size == 0);
}

void policy_limits_are_reported_without_parsing_sections() {
    const std::vector<std::uint8_t> bytes = make_bundle(valid_summary());
    DeviceBundleMemoryReader reader(bytes.data(), bytes.size());
    DeviceBundleDescriptor descriptor;

    DeviceBundleValidationPolicy too_small = policy();
    too_small.max_bundle_bytes = static_cast<std::uint32_t>(bytes.size() - 1);
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), too_small, descriptor) ==
           DeviceBundleStatus::PayloadTooLarge);

    DeviceBundleValidationPolicy no_entries = policy();
    no_entries.max_resource_entries = 0;
    assert(inspect_device_bundle(reader, static_cast<std::uint32_t>(bytes.size()), no_entries, descriptor) ==
           DeviceBundleStatus::PayloadTooLarge);

    std::vector<std::uint8_t> too_many = bytes;
    write_le32(too_many.data() + 28, 2);
    refresh_bundle_crc(too_many);
    DeviceBundleMemoryReader too_many_reader(too_many.data(), too_many.size());
    DeviceBundleValidationPolicy one_entry = policy();
    one_entry.max_resource_entries = 1;
    assert(inspect_device_bundle(too_many_reader,
                                 static_cast<std::uint32_t>(too_many.size()),
                                 one_entry,
                                 descriptor) == DeviceBundleStatus::TooManyResources);
}

} // namespace

int main() {
    validates_bundle_and_reads_resource();
    default_policy_is_safe_and_usable();
    rejects_bad_bundle_or_resource_checksum();
    rejects_ambiguous_or_incompatible_summary();
    read_failures_never_become_valid_bundles();
    resource_reads_stay_inside_the_declared_payload_section();
    policy_limits_are_reported_without_parsing_sections();
    return 0;
}
