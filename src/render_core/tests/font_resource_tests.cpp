#include "render_core/font_resource.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

using namespace jellyframe;
namespace {
using Bytes = std::vector<std::uint8_t>;
using S = FontResourceStatus;
void put16(Bytes& b, std::size_t at, unsigned v) { b[at] = v & 255; b[at+1] = (v >> 8) & 255; }
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (unsigned i=0; i<4; ++i) b[at+i] = (v >> (8*i)) & 255;
}
void seal(Bytes& b) {
    put32(b,96,0);
    std::uint32_t crc=0xffffffffU;
    for (auto value:b) {
        crc ^= value;
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1) ^ ((crc&1) ? 0xedb88320U : 0);
    }
    put32(b,96,~crc);
}
Bytes fixture() {
    // Space, j with negative bearing, zero-advance combining mark.
    Bytes b(128+3*40+3,0);
    const char magic[]="JFFONT2";
    for (unsigned i=0;i<8;++i) b[i]=magic[i];
    put16(b,8,2);put16(b,10,128);put16(b,12,40);put16(b,14,1);
    put32(b,16,static_cast<std::uint32_t>(b.size()));put32(b,20,3);
    put32(b,24,128);put32(b,28,248);put32(b,32,3);
    put16(b,36,1000);put16(b,38,16);put16(b,40,400);b[42]=4;
    put32(b,44,800);put32(b,48,200);put32(b,52,50);b[64]=1;
    put32(b,100,1);put16(b,104,1);
    put32(b,128,32);put32(b,132,1);put32(b,136,250);
    put32(b,168,'j');put32(b,172,2);put32(b,176,333);
    put32(b,180,static_cast<std::uint32_t>(-2));put32(b,184,static_cast<std::uint32_t>(-3));
    put16(b,188,1);put16(b,190,2);put32(b,192,1);put32(b,200,2);
    put32(b,208,0x301);put32(b,212,3);
    put32(b,224,static_cast<std::uint32_t>(-1));
    put16(b,228,2);put16(b,230,1);put32(b,232,1);put32(b,236,2);put32(b,240,1);
    b[248]=0xf0;b[249]=0x80;b[250]=0xff;
    seal(b);return b;
}
void refusals() {
    const auto good=fixture();
    FontResource font;
    assert(font.load(good.data(),good.size())==S::Loaded);
    auto reject=[&](auto mutate,S expected) {
        auto bad=good;mutate(bad);seal(bad);
        assert(font.load(bad.data(),bad.size())==expected);
        FontGlyphView g;
        assert(font.glyph_count()==3 && font.find_glyph('j',g) && g.coverage==good.data()+248);
    };
    assert(font.load(nullptr,0)==S::InvalidHeader);
    assert(font.load(good.data(),127)==S::InvalidHeader);
    for (std::size_t length=0;length<good.size();++length)
        assert(font.load(good.data(),length)!=S::Loaded);
    auto unaligned=good;unaligned.insert(unaligned.begin(),0);
    FontResource other;
    assert(other.load(unaligned.data()+1,good.size())==S::Loaded);
    reject([](Bytes& b){b[6]='1';},S::UnsupportedFormat);
    reject([](Bytes& b){put16(b,8,3);},S::UnsupportedFormat);
    reject([](Bytes& b){put16(b,14,2);},S::UnsupportedFormat);
    reject([](Bytes& b){put32(b,100,2);},S::UnsupportedFormat);
    reject([](Bytes& b){put16(b,104,2);},S::UnsupportedFormat);
    for (auto at:{10,12,16,24,28,32,43,60,106,127})
        reject([&](Bytes& b){b[at]^=1;},S::InvalidHeader);
    reject([](Bytes& b){put32(b,20,1000);},S::InvalidHeader);
    reject([](Bytes& b){put32(b,20,0);},S::ResourceLimit);
    for (auto at:{36,38,40}) reject([&](Bytes& b){put16(b,at,0);},S::InvalidMetrics);
    reject([](Bytes& b){b[42]=3;},S::InvalidMetrics);
    reject([](Bytes& b){put32(b,44,0xffffffffU);},S::InvalidMetrics);
    reject([](Bytes& b){put32(b,48,0x80000000U);},S::InvalidMetrics);
    reject([](Bytes& b){put32(b,52,static_cast<std::uint32_t>(-1001));},S::InvalidMetrics);
    reject([](Bytes& b){b[64]=0;},S::InvalidMetrics);
    reject([](Bytes& b){put32(b,56,0xd800);},S::InvalidMetrics);
    reject([](Bytes& b){put32(b,56,'X');},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,168,32);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,168,31);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,168,0xd800);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,208,0x110000);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,172,0);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,172,65536);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,176,0xffffffffU);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,180,0x7fffffff);},S::InvalidGlyph);
    reject([](Bytes& b){put32(b,144,1);},S::InvalidGlyph); // Empty ink bearing.
    reject([](Bytes& b){put16(b,188,0);},S::InvalidGlyph);
    reject([](Bytes& b){put16(b,188,4097);},S::InvalidGlyph);
    for (auto at:{192,196,200,204,236,240})
        reject([&](Bytes& b){b[at]^=1;},S::InvalidGlyph);
    reject([](Bytes& b){b[249]|=1;},S::InvalidGlyph); // Nonzero row padding.
    auto corrupt=good;corrupt.back()^=1;
    assert(font.load(corrupt.data(),corrupt.size())==S::ChecksumMismatch);
    FontResourceLimits limits;
    limits.max_bytes=good.size()-1;
    assert(font.load(good.data(),good.size(),limits)==S::ResourceLimit);
    limits={};limits.max_glyphs=2;
    assert(font.load(good.data(),good.size(),limits)==S::ResourceLimit);
    limits={};limits.max_work=good.size()+3+3-1;
    assert(font.load(good.data(),good.size(),limits)==S::ResourceLimit);
    ++limits.max_work;
    assert(font.load(good.data(),good.size(),limits)==S::Loaded);
    limits={};limits.max_bitmap_dimension=1;
    assert(font.load(good.data(),good.size(),limits)==S::InvalidGlyph);
    auto fallback=good;put32(fallback,56,'j');seal(fallback);
    assert(font.load(fallback.data(),fallback.size())==S::Loaded);
    FontGlyphView g;
    assert(!font.find_glyph('X',g)); // Declaration does not silently substitute.
}
void metrics() {
    auto b=fixture();FontResource font;FontScaledMetrics m;FontScaledGlyphMetrics gm;FontGlyphView g;
    assert(!font.metrics_at_size(16000,m));
    assert(font.load(b.data(),b.size())==S::Loaded);
    assert(font.metadata().ascent==800 && font.metadata().descent==200 && font.metadata().line_gap==50);
    assert(font.metrics_at_size(15500,m));
    assert(m.ascent==12400 && m.descent==3100 && m.line_gap==775 && m.line_height==16275);
    assert(font.glyph_metrics_at_size('j',15500,gm));
    assert(gm.advance==5162 && gm.bearing_x==-1938 && gm.bearing_y==-2906 && gm.width==969 && gm.height==1938);
    assert(font.find_glyph(32,g) && g.advance==250 && !g.coverage && !g.width && !g.height);
    assert(font.find_glyph(0x301,g) && g.advance==0 && g.bearing_y==-1);
    assert(font.find_glyph('j',g) && g.coverage_bytes==2 && g.coverage[0]==0xf0);
    assert(!font.find_glyph(0xd800,g) && !g.coverage && !g.advance);
    assert(!font.find_glyph('X',g));
    assert(font.metrics_at_size(0,m) && !m.line_height);
    assert(!font.metrics_at_size(-1,m) && !m.ascent);
    assert(!font.glyph_metrics_at_size('j',-1,gm) && !gm.width);
    assert(!font.metrics_at_size(std::numeric_limits<std::int32_t>::max(),m) && !m.ascent);
    assert(font.glyph_metrics_at_size('j',std::numeric_limits<std::int32_t>::max(),gm));
    put32(b,176,1U<<20);seal(b);
    assert(font.load(b.data(),b.size())==S::Loaded);
    assert(!font.glyph_metrics_at_size('j',std::numeric_limits<std::int32_t>::max(),gm) && !gm.advance);
    put32(b,176,333);
    // Exact +/- 0.5 Q1000 pixel rounds away from zero, including negative gaps.
    put16(b,36,2);put16(b,38,2);put32(b,44,1);put32(b,48,1);
    put32(b,52,static_cast<std::uint32_t>(-1));put32(b,180,static_cast<std::uint32_t>(-1));seal(b);
    assert(font.load(b.data(),b.size())==S::Loaded);
    assert(font.metrics_at_size(1,m) && m.ascent==1 && m.descent==1 && m.line_gap==-1 && m.line_height==1);
    assert(font.glyph_metrics_at_size('j',1,gm) && gm.advance==167 && gm.bearing_x==-1 && gm.width==1);
    font.clear();assert(!font.valid() && !font.glyph_count() && !font.metadata().units_per_em);
    assert(!font.find_glyph('j',g) && !g.coverage);
}
int inspect(const char* path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) return 2;
    Bytes data{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    FontResource font;
    if (font.load(data.data(),data.size())!=S::Loaded) return 3;
    const auto& m=font.metadata();FontScaledMetrics scaled;
    if (!font.metrics_at_size(15500,scaled)) return 4;
    std::cout<<"{\"units\":"<<m.units_per_em<<",\"size\":"<<m.raster_pixel_size
        <<",\"weight\":"<<m.weight<<",\"bits\":"<<unsigned(m.coverage_bits)
        <<",\"metrics\":["<<m.ascent<<','<<m.descent<<','<<m.line_gap<<"],\"scaled\":["
        <<scaled.ascent<<','<<scaled.descent<<','<<scaled.line_gap<<','<<scaled.line_height<<"],\"glyphs\":[";
    bool first=true;
    for (std::uint32_t cp=0;cp<=0x10ffff;++cp) {
        FontGlyphView g;FontScaledGlyphMetrics s;
        if (!font.find_glyph(cp,g)) continue;
        if (!font.glyph_metrics_at_size(cp,15500,s)) return 5;
        if (!first) std::cout<<',';first=false;
        std::cout<<'['<<cp<<','<<g.glyph_id<<','<<g.advance<<','<<g.bearing_x<<','<<g.bearing_y
            <<','<<g.width<<','<<g.height<<','<<s.advance<<','<<s.bearing_x<<','<<s.bearing_y<<','<<s.width<<','<<s.height<<']';
    }
    std::cout<<"]}\n";return 0;
}
} // namespace
int main(int argc,char** argv) {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    if (argc==2) return inspect(argv[1]);
    refusals();metrics();
    std::cout<<"font resource validation/scaling passed\n";
}
