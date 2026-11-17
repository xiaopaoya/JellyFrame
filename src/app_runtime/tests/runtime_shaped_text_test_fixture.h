#pragma once

#include "app_runtime/runtime_shaped_text_binding.h"

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)

#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
#include "runtime_contextual_fixture_digests.h"
#else
#include "shaping_catalog.h"
#include "shaping_catalog_sha256.h"
#endif

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace jellyframe::runtime_test_shaped_text {
using Bytes = std::vector<std::uint8_t>;

inline std::string load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), {}};
}

inline void put16(Bytes& bytes, std::size_t at, unsigned value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

inline void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

inline std::uint32_t crc(const Bytes& bytes) {
    std::uint32_t value = ~0U;
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        value ^= at >= 96 && at < 100 ? 0U : bytes[at];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
    }
    return ~value;
}

#if !JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
inline Bytes make_font(const shaping_catalog::Metrics& metrics,
                       std::uint16_t gid, const FontGlyphView& glyph) {
    Bytes bytes(168 + glyph.coverage_bytes, 0);
    std::memcpy(bytes.data(), "JFFONT2", 8);
    put16(bytes, 8, 2); put16(bytes, 10, 128); put16(bytes, 12, 40); put16(bytes, 14, 1);
    put32(bytes, 16, static_cast<std::uint32_t>(bytes.size())); put32(bytes, 20, 1);
    put32(bytes, 24, 128); put32(bytes, 28, 168);
    put32(bytes, 32, static_cast<std::uint32_t>(glyph.coverage_bytes));
    put16(bytes, 36, metrics.units_per_em); put16(bytes, 38, metrics.raster_pixel_size);
    put16(bytes, 40, metrics.weight); bytes[42] = 4;
    put32(bytes, 44, static_cast<std::uint32_t>(metrics.ascent));
    put32(bytes, 48, static_cast<std::uint32_t>(metrics.descent));
    put32(bytes, 52, static_cast<std::uint32_t>(metrics.line_gap));
    std::memcpy(bytes.data() + 64, metrics.source_sha256.data(), 32);
    put32(bytes, 100, 1); put16(bytes, 104, 1);
    put32(bytes, 128, 'A'); put32(bytes, 132, gid); put32(bytes, 136, glyph.advance);
    put32(bytes, 140, static_cast<std::uint32_t>(glyph.bearing_x));
    put32(bytes, 144, static_cast<std::uint32_t>(glyph.bearing_y));
    put16(bytes, 148, glyph.width); put16(bytes, 150, glyph.height);
    put32(bytes, 152, glyph.row_stride);
    put32(bytes, 160, static_cast<std::uint32_t>(glyph.coverage_bytes));
    std::copy_n(glyph.coverage, glyph.coverage_bytes, bytes.data() + 168);
    put32(bytes, 96, crc(bytes));
    return bytes;
}

#endif

inline RuntimeSha256 digest_from_hex(const char* hex) {
    RuntimeSha256 result{};
    for (std::size_t i=0;i<result.size();++i) {
        const auto nibble=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
        result[i]=static_cast<unsigned char>((nibble(hex[2*i])<<4)|nibble(hex[2*i+1]));
    }
    return result;
}

struct Assets {
    std::string font;
    std::string catalog;
    std::string companion;
    std::string tables;
    std::string eligibility;
    RuntimeShapedTextManifest manifest;

    explicit Assets(const char* face, std::uint64_t resource_id) {
        const std::string root = JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT;
        const std::string name = face;
        catalog = load(root + "/shaping_executor_catalog/" + name + ".jfcat1");
        companion = load(root + "/shaping_resource/" + name + ".jfshr2");
        tables = load(root + "/shaping_normalization/unicode14.bin");
        eligibility = load(root + "/shaping_normalization/" + name + ".eligibility");

        const auto source = std::string_view(catalog).substr(16, 32);
        const auto profile = std::string_view(catalog).substr(48, 32);
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
        font=load(root+"/contextual_font/"+name+".jffont2");
        const FixtureDigests* digests=nullptr;
        for (const auto& entry:fixture_digests) if (name==entry.face) digests=&entry;
        assert(digests);
        std::memcpy(manifest.source_identity.data(),source.data(),32);
        std::memcpy(manifest.profile_identity.data(),profile.data(),32);
        manifest.font_resource_identity=digest_from_hex(digests->font);
        manifest.catalog_identity=digest_from_hex(digests->catalog);
        manifest.companion_identity=digest_from_hex(digests->companion);
        manifest.tables_identity=digest_from_hex(digests->tables);
        manifest.eligibility_identity=digest_from_hex(digests->eligibility);
#else
        shaping_catalog::Identity identity;
        std::memcpy(identity.shaping.source, source.data(), 32);
        std::memcpy(identity.shaping.profile, profile.data(), 32);
        identity.shaping.glyph_count = 30900;
        identity.trusted_companion = companion;
        identity.companion_sha256 = shaping_catalog::sha256(companion);
        identity.catalog_sha256 = shaping_catalog::sha256(catalog);
        identity.weight = name == "bold" ? 700 : name == "medium" ? 500 : 400;
        shaping_catalog::View view;
        std::uint64_t work = 32U*1024U*1024U;
        assert(shaping_catalog::bind(catalog, identity, {}, work, view) ==
               shaping_catalog::Status::ok);
        std::uint16_t gid = 0;
        assert(view.map_scalar('A', true, gid));
        FontGlyphView glyph;
        assert(view.find_gid(gid, glyph));
        const auto bytes = make_font(view.metadata(), gid, glyph);
        font.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        std::memcpy(manifest.source_identity.data(), source.data(), 32);
        std::memcpy(manifest.profile_identity.data(), profile.data(), 32);
        manifest.font_resource_identity = shaping_catalog::sha256(font);
        manifest.catalog_identity = shaping_catalog::sha256(catalog);
        manifest.companion_identity = shaping_catalog::sha256(companion);
        manifest.tables_identity = shaping_catalog::sha256(tables);
        manifest.eligibility_identity = shaping_catalog::sha256(eligibility);
#endif
        manifest.shaping_glyph_count = 30900;
        manifest.resource_id = resource_id;
        manifest.raster_size = 16;
        manifest.weight = name == "bold" ? 700 : name == "medium" ? 500 : 400;
        manifest.selection = 0;
        update_views();
    }

    RuntimeSha256 digest_for(std::string_view path) const {
        if (path=="/font.jffont2") return manifest.font_resource_identity;
        if (path=="/catalog.jfcat") return manifest.catalog_identity;
        if (path=="/companion.jfshr") return manifest.companion_identity;
        if (path=="/tables.bin") return manifest.tables_identity;
        assert(path=="/eligibility.bin");
        return manifest.eligibility_identity;
    }

    void update_views() {
        manifest.font_resource = font;
        manifest.shaping_catalog = catalog;
        manifest.shaping_companion = companion;
        manifest.normalization_tables = tables;
        manifest.normalization_eligibility = eligibility;
    }

    void clear_source_buffers() {
        font.clear(); catalog.clear(); companion.clear(); tables.clear(); eligibility.clear();
    }
};

inline RuntimeShapedTextBinding::Handle create_binding(const Assets& assets) {
    RuntimeShapedTextBinding::Handle binding;
    assert(RuntimeShapedTextBinding::create(assets.manifest, binding) ==
           RuntimeShapedTextStatus::Ready);
    assert(binding && binding->valid());
    return binding;
}

} // namespace jellyframe::runtime_test_shaped_text

#endif
