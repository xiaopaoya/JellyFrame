#include "device_runtime_contracts/device_bundle.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::uint8_t kBundleMagic[] = {'J', 'F', 'A', 'P', 'P', 'V', '0', 0};
constexpr std::size_t kHeaderCrcOffset = 48;
constexpr std::size_t kHeaderReservedOffset = 52;
constexpr std::size_t kIoChunkBytes = 128;

constexpr std::uint32_t kCrc32Initial = 0xffffffffu;

std::uint16_t read_le16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t read_le32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

bool range_is_valid(std::uint32_t total, std::uint32_t offset, std::uint32_t size) {
    return offset <= total && size <= total - offset;
}

bool section_is_valid(std::uint32_t total, std::uint32_t offset, std::uint32_t size) {
    return offset >= kDeviceBundleHeaderBytes && range_is_valid(total, offset, size);
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

DeviceBundleStatus crc32_for_range(const DeviceBundleReader& reader,
                                   std::uint32_t offset,
                                   std::uint32_t size,
                                   std::uint32_t& crc) {
    std::array<std::uint8_t, kIoChunkBytes> bytes{};
    std::uint32_t state = kCrc32Initial;
    std::uint32_t consumed = 0;
    while (consumed < size) {
        const std::size_t chunk = std::min<std::size_t>(bytes.size(), size - consumed);
        if (!reader.read_at(offset + consumed, bytes.data(), chunk)) {
            return DeviceBundleStatus::ReadFailed;
        }
        state = crc32_extend(state, bytes.data(), chunk);
        consumed += static_cast<std::uint32_t>(chunk);
    }
    crc = ~state;
    return DeviceBundleStatus::Ok;
}

DeviceBundleStatus crc32_for_bundle(const DeviceBundleReader& reader,
                                    std::uint32_t bundle_bytes,
                                    std::uint32_t& crc) {
    std::array<std::uint8_t, kIoChunkBytes> bytes{};
    std::uint32_t state = kCrc32Initial;
    std::uint32_t consumed = 0;
    while (consumed < bundle_bytes) {
        const std::size_t chunk = std::min<std::size_t>(bytes.size(), bundle_bytes - consumed);
        if (!reader.read_at(consumed, bytes.data(), chunk)) {
            return DeviceBundleStatus::ReadFailed;
        }
        for (std::size_t index = 0; index < chunk; ++index) {
            const std::uint32_t absolute = consumed + static_cast<std::uint32_t>(index);
            if (absolute >= kHeaderCrcOffset && absolute < kHeaderCrcOffset + sizeof(std::uint32_t)) {
                bytes[index] = 0;
            }
        }
        state = crc32_extend(state, bytes.data(), chunk);
        consumed += static_cast<std::uint32_t>(chunk);
    }
    crc = ~state;
    return DeviceBundleStatus::Ok;
}

std::uint32_t fnv1a32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t value = 0x811c9dc5u;
    for (std::size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        value *= 0x01000193u;
    }
    return value;
}

bool path_is_normalized(const std::uint8_t* bytes, std::size_t size) {
    if (size < 2 || bytes[0] != '/') {
        return false;
    }
    bool segment_has_character = false;
    std::size_t segment_start = 1;
    for (std::size_t index = 1; index <= size; ++index) {
        const bool at_end = index == size;
        if (!at_end && (bytes[index] == 0 || bytes[index] == '\\')) {
            return false;
        }
        if (!at_end && bytes[index] != '/') {
            segment_has_character = true;
            continue;
        }
        if (!segment_has_character) {
            return false;
        }
        const std::size_t segment_size = index - segment_start;
        if ((segment_size == 1 && bytes[segment_start] == '.') ||
            (segment_size == 2 && bytes[segment_start] == '.' && bytes[segment_start + 1] == '.')) {
            return false;
        }
        segment_start = index + 1;
        segment_has_character = false;
    }
    return true;
}

bool string_has_control_character(std::string_view value) {
    for (const unsigned char character : value) {
        if (character < 0x20u) {
            return true;
        }
    }
    return false;
}

template <std::size_t Capacity>
bool copy_json_string(std::string_view source, std::size_t& index, std::array<char, Capacity>& output) {
    if (index >= source.size() || source[index++] != '"') {
        return false;
    }
    std::size_t size = 0;
    while (index < source.size()) {
        const unsigned char raw = static_cast<unsigned char>(source[index++]);
        if (raw < 0x20) {
            return false;
        }
        if (raw == '"') {
            if (size >= output.size()) {
                return false;
            }
            output[size] = '\0';
            return true;
        }
        char value = static_cast<char>(raw);
        if (value == '\\') {
            if (index >= source.size()) {
                return false;
            }
            switch (source[index++]) {
            case '"': value = '"'; break;
            case '\\': value = '\\'; break;
            case '/': value = '/'; break;
            case 'b': value = '\b'; break;
            case 'f': value = '\f'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            default:
                return false; // Device identity fields deliberately reject unicode escape aliases.
            }
        }
        if (size + 1 >= output.size()) {
            return false;
        }
        output[size++] = value;
    }
    return false;
}

class SummaryParser {
public:
    SummaryParser(std::string_view source, DeviceBundleSummary& summary) : source_(source), summary_(summary) {}

    bool parse() {
        skip_space();
        if (!consume('{')) {
            return false;
        }
        skip_space();
        if (consume('}')) {
            return false;
        }
        for (;;) {
            std::array<char, 32> key{};
            if (!copy_json_string(source_, index_, key)) {
                return false;
            }
            skip_space();
            if (!consume(':')) {
                return false;
            }
            skip_space();
            if (!parse_member(std::string_view(key.data()))) {
                return false;
            }
            skip_space();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return false;
            }
            skip_space();
        }
        skip_space();
        return index_ == source_.size() && required_fields_present() && summary_is_valid();
    }

private:
    enum SeenField : std::uint16_t {
        Id = 1u << 0,
        Name = 1u << 1,
        Role = 1u << 2,
        VersionName = 1u << 3,
        VersionCode = 1u << 4,
        Entry = 1u << 5,
        MinJellyFrame = 1u << 6,
        MinRenderCore = 1u << 7,
        Script = 1u << 8,
    };

    void skip_space() {
        while (index_ < source_.size()) {
            const char value = source_[index_];
            if (value != ' ' && value != '\n' && value != '\r' && value != '\t') {
                break;
            }
            ++index_;
        }
    }

    bool consume(char expected) {
        if (index_ >= source_.size() || source_[index_] != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    bool mark_once(SeenField field) {
        if ((seen_ & field) != 0) {
            return false;
        }
        seen_ = static_cast<std::uint16_t>(seen_ | field);
        return true;
    }

    bool parse_member(std::string_view key) {
        if (key == "id") {
            return mark_once(Id) && copy_json_string(source_, index_, summary_.app_id);
        }
        if (key == "name") {
            return mark_once(Name) && copy_json_string(source_, index_, summary_.app_name);
        }
        if (key == "versionName") {
            return mark_once(VersionName) && copy_json_string(source_, index_, summary_.version_name);
        }
        if (key == "entry") {
            return mark_once(Entry) && copy_json_string(source_, index_, summary_.entry_path);
        }
        if (key == "minJellyFrame") {
            return mark_once(MinJellyFrame) && copy_json_string(source_, index_, summary_.min_jellyframe);
        }
        if (key == "minRenderCore") {
            return mark_once(MinRenderCore) && copy_json_string(source_, index_, summary_.min_render_core);
        }
        if (key == "role") {
            std::array<char, 16> value{};
            if (!mark_once(Role) || !copy_json_string(source_, index_, value)) {
                return false;
            }
            const std::string_view role(value.data());
            if (role == "app") {
                summary_.role = DeviceBundleAppRole::App;
            } else if (role == "launcher") {
                summary_.role = DeviceBundleAppRole::Launcher;
            } else if (role == "watchface") {
                summary_.role = DeviceBundleAppRole::Watchface;
            } else if (role == "settings") {
                summary_.role = DeviceBundleAppRole::Settings;
            } else {
                return false;
            }
            return true;
        }
        if (key == "script") {
            std::array<char, 16> value{};
            if (!mark_once(Script) || !copy_json_string(source_, index_, value)) {
                return false;
            }
            const std::string_view mode(value.data());
            if (mode == "none") {
                summary_.script_mode = DeviceBundleScriptMode::None;
            } else if (mode == "classic") {
                summary_.script_mode = DeviceBundleScriptMode::Classic;
            } else {
                return false;
            }
            return true;
        }
        if (key == "versionCode") {
            if (!mark_once(VersionCode)) {
                return false;
            }
            return parse_uint32(summary_.version_code);
        }
        return skip_value(0);
    }

    bool parse_uint32(std::uint32_t& output) {
        if (index_ >= source_.size() || source_[index_] < '0' || source_[index_] > '9') {
            return false;
        }
        if (source_[index_] == '0') {
            ++index_;
            if (index_ < source_.size() && source_[index_] >= '0' && source_[index_] <= '9') {
                return false;
            }
            output = 0;
            return true;
        }
        std::uint32_t value = 0;
        while (index_ < source_.size() && source_[index_] >= '0' && source_[index_] <= '9') {
            const std::uint32_t digit = static_cast<std::uint32_t>(source_[index_] - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10u) {
                return false;
            }
            value = value * 10u + digit;
            ++index_;
        }
        output = value;
        return true;
    }

    bool skip_value(std::size_t depth) {
        if (depth > 16 || index_ >= source_.size()) {
            return false;
        }
        switch (source_[index_]) {
        case '{': return skip_object(depth + 1);
        case '[': return skip_array(depth + 1);
        case '"': return skip_string();
        case 't': return consume_literal("true");
        case 'f': return consume_literal("false");
        case 'n': return consume_literal("null");
        default: return skip_number();
        }
    }

    bool skip_string() {
        if (!consume('"')) {
            return false;
        }
        while (index_ < source_.size()) {
            const unsigned char value = static_cast<unsigned char>(source_[index_++]);
            if (value < 0x20) {
                return false;
            }
            if (value == '"') {
                return true;
            }
            if (value == '\\') {
                if (index_ >= source_.size()) {
                    return false;
                }
                const char escaped = source_[index_++];
                if (escaped == 'u') {
                    for (int digit = 0; digit < 4; ++digit) {
                        if (index_ >= source_.size()) {
                            return false;
                        }
                        const unsigned char hex = static_cast<unsigned char>(source_[index_++]);
                        if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') ||
                              (hex >= 'A' && hex <= 'F'))) {
                            return false;
                        }
                    }
                } else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' && escaped != 'f' &&
                           escaped != 'n' && escaped != 'r' && escaped != 't') {
                    return false;
                }
            }
        }
        return false;
    }

    bool skip_object(std::size_t depth) {
        if (!consume('{')) {
            return false;
        }
        skip_space();
        if (consume('}')) {
            return true;
        }
        for (;;) {
            if (!skip_string()) {
                return false;
            }
            skip_space();
            if (!consume(':')) {
                return false;
            }
            skip_space();
            if (!skip_value(depth)) {
                return false;
            }
            skip_space();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip_space();
        }
    }

    bool skip_array(std::size_t depth) {
        if (!consume('[')) {
            return false;
        }
        skip_space();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!skip_value(depth)) {
                return false;
            }
            skip_space();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip_space();
        }
    }

    bool consume_literal(std::string_view value) {
        if (source_.substr(index_, value.size()) != value) {
            return false;
        }
        index_ += value.size();
        return true;
    }

    bool skip_number() {
        const std::size_t begin = index_;
        if (index_ < source_.size() && source_[index_] == '-') {
            ++index_;
        }
        if (index_ >= source_.size()) {
            return false;
        }
        if (source_[index_] == '0') {
            ++index_;
        } else if (source_[index_] >= '1' && source_[index_] <= '9') {
            do {
                ++index_;
            } while (index_ < source_.size() && source_[index_] >= '0' && source_[index_] <= '9');
        } else {
            return false;
        }
        if (index_ < source_.size() && source_[index_] == '.') {
            ++index_;
            const std::size_t fraction = index_;
            while (index_ < source_.size() && source_[index_] >= '0' && source_[index_] <= '9') {
                ++index_;
            }
            if (fraction == index_) {
                return false;
            }
        }
        if (index_ < source_.size() && (source_[index_] == 'e' || source_[index_] == 'E')) {
            ++index_;
            if (index_ < source_.size() && (source_[index_] == '+' || source_[index_] == '-')) {
                ++index_;
            }
            const std::size_t exponent = index_;
            while (index_ < source_.size() && source_[index_] >= '0' && source_[index_] <= '9') {
                ++index_;
            }
            if (exponent == index_) {
                return false;
            }
        }
        return index_ > begin;
    }

    bool required_fields_present() const {
        constexpr std::uint16_t required = Id | Name | Role | VersionName | VersionCode | Entry | MinJellyFrame |
            MinRenderCore | Script;
        return (seen_ & required) == required;
    }

    bool summary_is_valid() const {
        if (summary_.app_id_view().empty() || summary_.app_name_view().empty() || summary_.version_name_view().empty() ||
            summary_.entry_path_view().empty() || std::string_view(summary_.min_jellyframe.data()).empty() ||
            std::string_view(summary_.min_render_core.data()).empty()) {
            return false;
        }
        if (string_has_control_character(summary_.app_id_view()) ||
            string_has_control_character(summary_.app_name_view()) ||
            string_has_control_character(summary_.version_name_view()) ||
            string_has_control_character(summary_.entry_path_view()) ||
            string_has_control_character(std::string_view(summary_.min_jellyframe.data())) ||
            string_has_control_character(std::string_view(summary_.min_render_core.data()))) {
            return false;
        }
        return path_is_normalized(reinterpret_cast<const std::uint8_t*>(summary_.entry_path.data()),
                                  summary_.entry_path_view().size());
    }

    std::string_view source_;
    DeviceBundleSummary& summary_;
    std::size_t index_ = 0;
    std::uint16_t seen_ = 0;
};

DeviceBundleStatus validate_resource(const DeviceBundleReader& reader,
                                     const DeviceBundleDescriptor& descriptor,
                                     std::uint32_t index,
                                     std::string_view expected_path,
                                     DeviceBundleResource* resource) {
    std::array<std::uint8_t, kDeviceBundleResourceEntryBytes> raw{};
    const std::uint64_t offset64 = static_cast<std::uint64_t>(descriptor.index_offset) +
        static_cast<std::uint64_t>(index) * kDeviceBundleResourceEntryBytes;
    if (offset64 > std::numeric_limits<std::uint32_t>::max() ||
        !reader.read_at(static_cast<std::uint32_t>(offset64), raw.data(), raw.size())) {
        return DeviceBundleStatus::ReadFailed;
    }
    const std::uint32_t path_hash = read_le32(raw.data());
    const std::uint32_t path_offset = read_le32(raw.data() + 4);
    const std::uint16_t path_bytes = read_le16(raw.data() + 8);
    const std::uint16_t kind = read_le16(raw.data() + 10);
    const std::uint32_t payload_offset = read_le32(raw.data() + 12);
    const std::uint32_t payload_bytes = read_le32(raw.data() + 16);
    const std::uint32_t payload_crc32 = read_le32(raw.data() + 20);
    const std::uint32_t flags = read_le32(raw.data() + 24);
    if (flags != 0 || path_bytes == 0 || path_bytes > kDeviceBundleMaxResourcePathBytes ||
        !range_is_valid(descriptor.string_table_bytes, path_offset, path_bytes) ||
        !range_is_valid(descriptor.payload_bytes, payload_offset, payload_bytes)) {
        return DeviceBundleStatus::InvalidResource;
    }

    std::array<std::uint8_t, kDeviceBundleMaxResourcePathBytes> path{};
    if (!reader.read_at(descriptor.string_table_offset + path_offset, path.data(), path_bytes)) {
        return DeviceBundleStatus::ReadFailed;
    }
    if (!path_is_normalized(path.data(), path_bytes) || fnv1a32(path.data(), path_bytes) != path_hash) {
        return DeviceBundleStatus::InvalidResource;
    }
    if (!expected_path.empty() &&
        (expected_path.size() != path_bytes || !std::equal(path.begin(), path.begin() + path_bytes, expected_path.begin()))) {
        return DeviceBundleStatus::ResourceNotFound;
    }

    std::uint32_t actual_crc32 = 0;
    const DeviceBundleStatus checksum = crc32_for_range(reader,
                                                         descriptor.payload_offset + payload_offset,
                                                         payload_bytes,
                                                         actual_crc32);
    if (checksum != DeviceBundleStatus::Ok) {
        return checksum;
    }
    if (actual_crc32 != payload_crc32) {
        return DeviceBundleStatus::BadChecksum;
    }
    if (resource != nullptr) {
        *resource = DeviceBundleResource{kind, descriptor.payload_offset + payload_offset, payload_bytes, payload_crc32};
    }
    return DeviceBundleStatus::Ok;
}

} // namespace

bool DeviceBundleMemoryReader::read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const {
    if ((size != 0 && (bytes_ == nullptr || output == nullptr)) || offset > size_ || size > size_ - offset) {
        return false;
    }
    if (size != 0) {
        std::memcpy(output, bytes_ + offset, size);
    }
    return true;
}

std::string_view DeviceBundleSummary::app_id_view() const {
    return std::string_view(app_id.data());
}

std::string_view DeviceBundleSummary::app_name_view() const {
    return std::string_view(app_name.data());
}

std::string_view DeviceBundleSummary::version_name_view() const {
    return std::string_view(version_name.data());
}

std::string_view DeviceBundleSummary::entry_path_view() const {
    return std::string_view(entry_path.data());
}

DeviceBundleStatus inspect_device_bundle(const DeviceBundleReader& reader,
                                         std::uint32_t bundle_bytes,
                                         const DeviceBundleValidationPolicy& policy,
                                         DeviceBundleDescriptor& descriptor) {
    descriptor = {};
    if (policy.max_bundle_bytes == 0 || policy.max_resource_entries == 0 || bundle_bytes < kDeviceBundleHeaderBytes ||
        bundle_bytes > policy.max_bundle_bytes) {
        return DeviceBundleStatus::PayloadTooLarge;
    }

    std::array<std::uint8_t, kDeviceBundleHeaderBytes> header{};
    if (!reader.read_at(0, header.data(), header.size())) {
        return DeviceBundleStatus::ReadFailed;
    }
    if (!std::equal(std::begin(kBundleMagic), std::end(kBundleMagic), header.begin())) {
        return DeviceBundleStatus::InvalidMagic;
    }
    if (read_le16(header.data() + 8) != kDeviceBundleHeaderBytes || read_le16(header.data() + 10) != 0) {
        return DeviceBundleStatus::UnsupportedVersion;
    }
    if (read_le32(header.data() + 12) != 0 || read_le32(header.data() + kHeaderReservedOffset) != 0) {
        return DeviceBundleStatus::InvalidHeader;
    }

    descriptor.bundle_bytes = bundle_bytes;
    descriptor.summary_offset = read_le32(header.data() + 16);
    descriptor.summary_bytes = read_le32(header.data() + 20);
    descriptor.index_offset = read_le32(header.data() + 24);
    descriptor.resource_count = read_le32(header.data() + 28);
    descriptor.string_table_offset = read_le32(header.data() + 32);
    descriptor.string_table_bytes = read_le32(header.data() + 36);
    descriptor.payload_offset = read_le32(header.data() + 40);
    descriptor.payload_bytes = read_le32(header.data() + 44);
    descriptor.bundle_crc32 = read_le32(header.data() + kHeaderCrcOffset);

    const std::uint64_t index_bytes = static_cast<std::uint64_t>(descriptor.resource_count) *
        kDeviceBundleResourceEntryBytes;
    if (descriptor.resource_count > policy.max_resource_entries) {
        return DeviceBundleStatus::TooManyResources;
    }
    if (descriptor.bundle_crc32 == 0 || descriptor.summary_bytes == 0 ||
        descriptor.summary_bytes > policy.max_summary_bytes || descriptor.summary_bytes > kDeviceBundleMaxSummaryBytes ||
        index_bytes > std::numeric_limits<std::uint32_t>::max() ||
        !section_is_valid(bundle_bytes, descriptor.summary_offset, descriptor.summary_bytes) ||
        !section_is_valid(bundle_bytes, descriptor.index_offset, static_cast<std::uint32_t>(index_bytes)) ||
        !section_is_valid(bundle_bytes, descriptor.string_table_offset, descriptor.string_table_bytes) ||
        !section_is_valid(bundle_bytes, descriptor.payload_offset, descriptor.payload_bytes)) {
        return DeviceBundleStatus::InvalidSection;
    }

    std::uint32_t actual_crc32 = 0;
    const DeviceBundleStatus bundle_crc_status = crc32_for_bundle(reader, bundle_bytes, actual_crc32);
    if (bundle_crc_status != DeviceBundleStatus::Ok) {
        return bundle_crc_status;
    }
    if (actual_crc32 != descriptor.bundle_crc32) {
        return DeviceBundleStatus::BadChecksum;
    }

    std::array<std::uint8_t, kDeviceBundleMaxSummaryBytes> summary_bytes{};
    if (!reader.read_at(descriptor.summary_offset, summary_bytes.data(), descriptor.summary_bytes)) {
        return DeviceBundleStatus::ReadFailed;
    }
    if (!SummaryParser(std::string_view(reinterpret_cast<const char*>(summary_bytes.data()), descriptor.summary_bytes),
                       descriptor.summary).parse()) {
        return DeviceBundleStatus::InvalidSummary;
    }
    if ((!policy.required_min_jellyframe.empty() &&
         std::string_view(descriptor.summary.min_jellyframe.data()) != policy.required_min_jellyframe) ||
        (!policy.required_min_render_core.empty() &&
         std::string_view(descriptor.summary.min_render_core.data()) != policy.required_min_render_core)) {
        return DeviceBundleStatus::VersionRejected;
    }

    for (std::uint32_t index = 0; index < descriptor.resource_count; ++index) {
        const DeviceBundleStatus resource_status = validate_resource(reader, descriptor, index, {}, nullptr);
        if (resource_status != DeviceBundleStatus::Ok) {
            return resource_status;
        }
    }
    return DeviceBundleStatus::Ok;
}

DeviceBundleStatus find_device_bundle_resource(const DeviceBundleReader& reader,
                                               const DeviceBundleDescriptor& descriptor,
                                               std::string_view app_path,
                                               DeviceBundleResource& resource) {
    resource = {};
    if (app_path.empty() || app_path.size() > kDeviceBundleMaxResourcePathBytes ||
        !path_is_normalized(reinterpret_cast<const std::uint8_t*>(app_path.data()), app_path.size())) {
        return DeviceBundleStatus::InvalidArgument;
    }
    for (std::uint32_t index = 0; index < descriptor.resource_count; ++index) {
        DeviceBundleResource candidate;
        const DeviceBundleStatus status = validate_resource(reader, descriptor, index, app_path, &candidate);
        if (status == DeviceBundleStatus::ResourceNotFound) {
            continue;
        }
        if (status != DeviceBundleStatus::Ok) {
            return status;
        }
        resource = candidate;
        return DeviceBundleStatus::Ok;
    }
    return DeviceBundleStatus::ResourceNotFound;
}

DeviceBundleStatus read_device_bundle_resource(const DeviceBundleReader& reader,
                                               const DeviceBundleDescriptor& descriptor,
                                               const DeviceBundleResource& resource,
                                               std::uint8_t* output,
                                               std::size_t output_capacity,
                                               std::size_t& output_size) {
    output_size = 0;
    if ((resource.payload_bytes != 0 && output == nullptr) || resource.payload_bytes > output_capacity ||
        resource.payload_offset < descriptor.payload_offset ||
        !range_is_valid(descriptor.payload_bytes,
                        resource.payload_offset - descriptor.payload_offset,
                        resource.payload_bytes)) {
        return DeviceBundleStatus::BufferTooSmall;
    }
    if (!reader.read_at(resource.payload_offset, output, resource.payload_bytes)) {
        return DeviceBundleStatus::ReadFailed;
    }
    std::uint32_t actual_crc32 = 0;
    const DeviceBundleStatus checksum = crc32_for_range(reader, resource.payload_offset, resource.payload_bytes, actual_crc32);
    if (checksum != DeviceBundleStatus::Ok) {
        return checksum;
    }
    if (actual_crc32 != resource.payload_crc32) {
        return DeviceBundleStatus::BadChecksum;
    }
    output_size = resource.payload_bytes;
    return DeviceBundleStatus::Ok;
}

const char* device_bundle_status_name(DeviceBundleStatus status) {
    switch (status) {
    case DeviceBundleStatus::Ok: return "ok";
    case DeviceBundleStatus::InvalidArgument: return "invalid-argument";
    case DeviceBundleStatus::ReadFailed: return "read-failed";
    case DeviceBundleStatus::PayloadTooLarge: return "payload-too-large";
    case DeviceBundleStatus::Truncated: return "truncated";
    case DeviceBundleStatus::InvalidMagic: return "invalid-magic";
    case DeviceBundleStatus::UnsupportedVersion: return "unsupported-version";
    case DeviceBundleStatus::InvalidHeader: return "invalid-header";
    case DeviceBundleStatus::InvalidSection: return "invalid-section";
    case DeviceBundleStatus::TooManyResources: return "too-many-resources";
    case DeviceBundleStatus::InvalidResource: return "invalid-resource";
    case DeviceBundleStatus::BadChecksum: return "bad-checksum";
    case DeviceBundleStatus::InvalidSummary: return "invalid-summary";
    case DeviceBundleStatus::VersionRejected: return "version-rejected";
    case DeviceBundleStatus::ResourceNotFound: return "resource-not-found";
    case DeviceBundleStatus::BufferTooSmall: return "buffer-too-small";
    }
    return "invalid-argument";
}

const char* device_bundle_app_role_name(DeviceBundleAppRole role) {
    switch (role) {
    case DeviceBundleAppRole::App: return "app";
    case DeviceBundleAppRole::Launcher: return "launcher";
    case DeviceBundleAppRole::Watchface: return "watchface";
    case DeviceBundleAppRole::Settings: return "settings";
    }
    return "app";
}

const char* device_bundle_script_mode_name(DeviceBundleScriptMode mode) {
    switch (mode) {
    case DeviceBundleScriptMode::None: return "none";
    case DeviceBundleScriptMode::Classic: return "classic";
    }
    return "none";
}

} // namespace jellyframe
