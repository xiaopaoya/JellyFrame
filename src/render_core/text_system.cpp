#include "render_core/text_system.h"
#include "utf8_scalar.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>

namespace jellyframe {
namespace {
using Status = TextSystemStatus;
constexpr std::size_t max_faces = 64, max_families = 16;
struct Work {
    std::size_t remaining;
    bool take(std::size_t n = 1) noexcept {
        if (n > remaining) return false;
        remaining -= n; return true;
    }
};
struct Candidates {
    std::array<std::size_t, max_faces> indices{};
    std::size_t count = 0;
};
// CSS Fonts weight matching: 400..500 searches upward to 500, then below the
// request, then above 500; lighter/heavier requests prefer their own direction.
unsigned weight_rank(unsigned actual, unsigned wanted) noexcept {
    if (wanted < 400) return actual <= wanted ? wanted-actual : 1000+actual-wanted;
    if (wanted > 500) return actual >= wanted ? actual-wanted : 1000+wanted-actual;
    if (actual >= wanted && actual <= 500) return actual-wanted;
    return actual < wanted ? 1000+wanted-actual : 2000+actual-500;
}
std::int64_t size_distance(const TextFontFace& face, std::int32_t size) noexcept {
    const auto d = static_cast<std::int64_t>(face.resource.metadata().raster_pixel_size)*1000-size;
    return d < 0 ? -d : d;
}
bool better_strike(const TextFontFace& a, const TextFontFace& b, std::int32_t size) noexcept {
    const auto da=size_distance(a,size), db=size_distance(b,size);
    if (da != db) return da < db;
    const auto& am=a.resource.metadata();const auto& bm=b.resource.metadata();
    if (am.raster_pixel_size != bm.raster_pixel_size) return am.raster_pixel_size > bm.raster_pixel_size;
    return am.coverage_bits > bm.coverage_bits;
}
Status candidates(const TextFontFace* faces, std::size_t count, const TextFontRequest& font,
                  Work& work, Candidates& out) noexcept {
    std::array<std::uint32_t,max_families+1> families{};
    std::size_t family_count=0;
    for (std::size_t i=0;i<font.family_count;++i) {
        if (!work.take(family_count+1)) return Status::ResourceLimit;
        if (std::find(families.begin(),families.begin()+family_count,font.families[i])==families.begin()+family_count)
            families[family_count++]=font.families[i];
    }
    if (std::find(families.begin(),families.begin()+family_count,std::uint32_t{0})==families.begin()+family_count)
        families[family_count++]=0;
    for (std::size_t f=0;f<family_count;++f) {
        unsigned best=std::numeric_limits<unsigned>::max();
        std::uint16_t selected_weight=0;
        for (std::size_t i=0;i<count;++i) {
            if (!work.take()) return Status::ResourceLimit;
            if (faces[i].family_id != families[f]) continue;
            const auto weight=faces[i].resource.metadata().weight;
            const auto rank=weight_rank(weight,font.weight);
            if (rank<best) { best=rank;selected_weight=weight; }
        }
        const auto first=out.count;
        for (std::size_t i=0;i<count;++i) {
            if (!work.take()) return Status::ResourceLimit;
            if (faces[i].family_id==families[f] && faces[i].resource.metadata().weight==selected_weight)
                out.indices[out.count++]=i;
        }
        // Bounded insertion sort, stable registration order at exact ties.
        for (std::size_t i=first+1;i<out.count;++i) {
            auto at=i;
            while (at>first) {
                if (!work.take()) return Status::ResourceLimit;
                if (!better_strike(faces[out.indices[at]],faces[out.indices[at-1]],font.size_q1000)) break;
                std::swap(out.indices[at],out.indices[at-1]);--at;
            }
        }
    }
    return out.count ? Status::Ok : Status::InvalidFontSet;
}
// Conservative admission until the common shaper/Unicode integration lands.
// No scalar fallback for Arabic/Indic/bidi/combining/ZWJ/variation sequences.
bool simple_horizontal(std::uint32_t cp) noexcept {
    return (cp>=0x20 && cp<=0x7e) || cp==0xa0 || cp==0x2026 ||
        (cp>=0x3000 && cp<=0x3029) || (cp>=0x3030 && cp<=0x303f) ||
        (cp>=0x3400 && cp<=0x4dbf) ||
        (cp>=0x4e00 && cp<=0x9fff) || (cp>=0xf900 && cp<=0xfaff) ||
        (cp>=0xff01 && cp<=0xff60) || (cp>=0xffe0 && cp<=0xffe6);
}
std::size_t lookup_work(const FontResource& font) noexcept {
    std::size_t n=font.glyph_count(),steps=1;
    while (n) { ++steps;n>>=1; }
    return steps;
}
Status resolve(const TextFontFace* faces, const Candidates& choices, std::uint32_t cp,
               Work& work, std::size_t& selected, FontGlyphView& glyph,
               std::uint32_t& mapped) noexcept {
    mapped=cp;
    for (std::size_t i=0;i<choices.count;++i) {
        const auto index=choices.indices[i];const auto& resource=faces[index].resource;
        if (!work.take(lookup_work(resource))) return Status::ResourceLimit;
        if (resource.find_glyph(cp,glyph)) { selected=index;return Status::Ok; }
    }
    // Substitution is considered only after real coverage in all requested /
    // default families, and uses only the primary selected resource's declaration.
    selected=choices.indices[0];const auto& primary=faces[selected].resource;
    mapped=primary.metadata().fallback_codepoint;
    if (!mapped) return Status::MissingGlyph;
    if (!work.take(lookup_work(primary))) return Status::ResourceLimit;
    return primary.find_glyph(mapped,glyph) ? Status::Ok : Status::InvalidFontSet;
}
bool fits(std::int64_t value) noexcept {
    return value>=std::numeric_limits<std::int32_t>::min() && value<=std::numeric_limits<std::int32_t>::max();
}
Status walk(std::string_view text, const TextFontRequest& font,
            const TextFontFace* faces, const Candidates& choices,
            const TextSystemLimits& limits, Work& work, PreparedTextGlyph* output,
            TextRunMetrics& metrics) noexcept {
    const auto primary=choices.indices[0];
    metrics.primary_resource_id=faces[primary].resource_id;
    if (!faces[primary].resource.metrics_at_size(font.size_q1000,metrics.primary_font)) return Status::Overflow;
    metrics.ascent=metrics.primary_font.ascent;metrics.descent=metrics.primary_font.descent;
    metrics.natural_height=metrics.primary_font.line_height;
    std::size_t position=0;
    while (position<text.size()) {
        const auto begin=position;
        std::uint32_t cp=0;
        if (!detail::read_utf8_scalar(text,position,cp)) return Status::InvalidUtf8;
        if (!work.take(position-begin)) return Status::ResourceLimit;
        if (!simple_horizontal(cp)) return Status::UnsupportedText;
        if (metrics.glyph_count>=limits.max_glyphs) return Status::ResourceLimit;
        std::size_t selected=0;FontGlyphView glyph;std::uint32_t mapped=0;
        auto status=resolve(faces,choices,cp,work,selected,glyph,mapped);
        if (status!=Status::Ok) return status;
        const auto& face=faces[selected];
        if (!work.take(lookup_work(face.resource)+1)) return Status::ResourceLimit;
        PreparedTextGlyph prepared;
        prepared.resource_id=face.resource_id;prepared.glyph_id=glyph.glyph_id;
        prepared.codepoint=mapped;prepared.source_codepoint=cp;
        prepared.source_begin=static_cast<std::uint32_t>(begin);prepared.source_end=static_cast<std::uint32_t>(position);
        prepared.x=metrics.advance;
        FontScaledMetrics vertical;
        if (!face.resource.glyph_metrics_at_size(mapped,font.size_q1000,prepared.geometry) ||
            !face.resource.metrics_at_size(font.size_q1000,vertical)) return Status::Overflow;
        const auto advance=static_cast<std::int64_t>(metrics.advance)+prepared.geometry.advance;
        const auto left=static_cast<std::int64_t>(prepared.x)+prepared.geometry.bearing_x;
        const auto right=left+prepared.geometry.width;
        const auto bottom=static_cast<std::int64_t>(prepared.geometry.bearing_y)+prepared.geometry.height;
        if (!fits(advance) || !fits(left) || !fits(right) || !fits(bottom)) return Status::Overflow;
        if (prepared.geometry.width && prepared.geometry.height) {
            const auto l=static_cast<std::int32_t>(left),r=static_cast<std::int32_t>(right);
            const auto t=prepared.geometry.bearing_y,b=static_cast<std::int32_t>(bottom);
            if (!metrics.has_ink) { metrics.ink_left=l;metrics.ink_right=r;metrics.ink_top=t;metrics.ink_bottom=b; }
            else {
                metrics.ink_left=std::min(metrics.ink_left,l);metrics.ink_right=std::max(metrics.ink_right,r);
                metrics.ink_top=std::min(metrics.ink_top,t);metrics.ink_bottom=std::max(metrics.ink_bottom,b);
            }
            metrics.has_ink=true;
        }
        metrics.ascent=std::max(metrics.ascent,vertical.ascent);
        metrics.descent=std::max(metrics.descent,vertical.descent);
        metrics.natural_height=std::max(metrics.natural_height,vertical.line_height);
        metrics.advance=static_cast<std::int32_t>(advance);
        if (mapped!=cp) ++metrics.substitutions;
        if (output) output[metrics.glyph_count]=prepared;
        ++metrics.glyph_count;
    }
    return Status::Ok;
}
} // namespace

TextSystemStatus TextSystem::bind(const TextFontFace* faces, std::size_t count,
                                  TextSystemLimits limits) noexcept {
    if (generation_ == UINT64_MAX) return Status::Overflow;
    if (!faces || !count) return Status::InvalidFontSet;
    if (count>max_faces || count>limits.max_faces) return Status::ResourceLimit;
    Work work{limits.max_work};bool has_default=false;
    for (std::size_t i=0;i<count;++i) {
        if (!work.take()) return Status::ResourceLimit;
        if (!faces[i].resource_id || !faces[i].resource.valid()) return Status::InvalidFontSet;
        if (faces[i].css_family_hash && faces[i].family_id == 0) return Status::InvalidFontSet;
        has_default|=faces[i].family_id==0;
        const auto& a=faces[i].resource.metadata();
        for (std::size_t j=0;j<i;++j) {
            if (!work.take()) return Status::ResourceLimit;
            if (faces[i].resource_id==faces[j].resource_id) return Status::InvalidFontSet;
            if (faces[i].css_family_hash && faces[i].css_family_hash==faces[j].css_family_hash &&
                faces[i].family_id!=faces[j].family_id) return Status::InvalidFontSet;
            const auto& b=faces[j].resource.metadata();
            if (a.source_sha256==b.source_sha256 &&
                (a.units_per_em!=b.units_per_em || a.weight!=b.weight || a.ascent!=b.ascent ||
                 a.descent!=b.descent || a.line_gap!=b.line_gap)) return Status::InvalidFontSet;
        }
    }
    if (!has_default) return Status::InvalidFontSet;
    faces_=faces;count_=count;limits_=limits;++generation_;return Status::Ok;
}

bool TextSystem::resolve_family_hash(std::uint32_t hash, std::uint32_t& family_id) const noexcept {
    if (!ready()) return false;
    if (hash == 0) { family_id = 0; return true; }
    for (std::size_t i = 0; i < count_; ++i) {
        if (faces_[i].css_family_hash != hash) continue;
        family_id = faces_[i].family_id;
        return true;
    }
    return false;
}

TextSystemStatus TextSystem::process(std::string_view text, const TextFontRequest& font,
    PreparedTextGlyph* output, std::size_t capacity, bool emit, TextRunMetrics& out) const noexcept {
    out={};
    if (!ready()) return Status::NotReady;
    if (font.size_q1000<0 || !font.weight || font.weight>1000 ||
        (font.family_count && !font.families) || (!text.empty() && !text.data())) return Status::InvalidRequest;
    if (font.style!=TextFaceStyle::Normal) return Status::UnsupportedStyle;
    if (font.family_count>max_families || font.family_count>limits_.max_families ||
        text.size()>limits_.max_text_bytes || text.size()>std::numeric_limits<std::uint32_t>::max()) return Status::ResourceLimit;
    Work work{limits_.max_work};Candidates choices;
    auto status=candidates(faces_,count_,font,work,choices);
    if (status!=Status::Ok) return status;
    const auto before=work.remaining;
    TextRunMetrics draft;
    status=walk(text,font,faces_,choices,limits_,work,nullptr,draft);
    if (status!=Status::Ok) return status;
    if (emit) {
        if (draft.glyph_count>capacity || (draft.glyph_count && !output)) return Status::OutputTooSmall;
        if (!work.take(before-work.remaining)) return Status::ResourceLimit;
        if (draft.glyph_count) {
            // Source/registrations/resources/output non-aliasing and immutability
            // are required for both passes. No failure remains after preflight.
            TextRunMetrics emitted;Work replay{before};
            status=walk(text,font,faces_,choices,limits_,replay,output,emitted);
            assert(status==Status::Ok);
            if (status!=Status::Ok) return status;
        }
    }
    out=draft;return Status::Ok;
}
TextSystemStatus TextSystem::measure(std::string_view text,const TextFontRequest& font,
                                    TextRunMetrics& out) const noexcept {
    return process(text,font,nullptr,0,false,out);
}
TextSystemStatus TextSystem::prepare(std::string_view text,const TextFontRequest& font,
    PreparedTextGlyph* output,std::size_t capacity,TextRunMetrics& metrics) const noexcept {
    return process(text,font,output,capacity,true,metrics);
}
} // namespace jellyframe
