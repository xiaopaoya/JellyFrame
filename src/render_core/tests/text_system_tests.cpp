#include "render_core/text_system.h"
#include "render_core/prepared_text.h"
#include "render_core/software_renderer.h"
#include "render_core/css_parser.h"
#include "render_core/diagnostics.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/layer_tree.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <utility>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

// This isolated executable verifies no ordinary new/new[] during service calls.
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    ++allocations;
    if (auto* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

using namespace jellyframe;
namespace {
using S=TextSystemStatus;
using Bytes=std::vector<std::uint8_t>;
void u16(Bytes& b,std::size_t at,unsigned v) { b[at]=v&255;b[at+1]=(v>>8)&255; }
void u32(Bytes& b,std::size_t at,std::uint32_t v) {
    for (unsigned i=0;i<4;++i) b[at+i]=(v>>(8*i))&255;
}
struct Asset { Bytes bytes;FontResource font; };
Asset asset(unsigned weight=400,unsigned size=16,unsigned bits=4,unsigned tag=1,
            std::vector<std::pair<unsigned,unsigned>> glyphs={{32,250},{65,500},{66,600}},
            unsigned fallback=0,unsigned ascent=800) {
    Asset a;
    const auto bitmap=128+glyphs.size()*40;
    a.bytes.resize(bitmap,0);auto& b=a.bytes;
    const char magic[]="JFFONT2";for (unsigned i=0;i<8;++i) b[i]=magic[i];
    u16(b,8,2);u16(b,10,128);u16(b,12,40);u16(b,14,1);
    u32(b,20,static_cast<unsigned>(glyphs.size()));u32(b,24,128);u32(b,28,static_cast<unsigned>(bitmap));
    u16(b,36,1000);u16(b,38,size);u16(b,40,weight);b[42]=static_cast<std::uint8_t>(bits);
    u32(b,44,ascent);u32(b,48,200);u32(b,52,50);u32(b,56,fallback);b[64]=static_cast<std::uint8_t>(tag);
    u32(b,100,1);u16(b,104,1);
    for (std::size_t i=0;i<glyphs.size();++i) {
        auto at=128+i*40;u32(b,at,glyphs[i].first);u32(b,at+4,static_cast<unsigned>(i+1));u32(b,at+8,glyphs[i].second);
        u32(b,at+28,static_cast<unsigned>(b.size()-bitmap));
        if (glyphs[i].first!=32) {
            u32(b,at+12,static_cast<unsigned>(-1));u32(b,at+16,static_cast<unsigned>(-2));
            u16(b,at+20,1);u16(b,at+22,2);u32(b,at+24,1);u32(b,at+32,2);
            b.push_back(static_cast<std::uint8_t>(((1U<<bits)-1)<<(8-bits)));b.push_back(b.back());
        }
    }
    u32(b,16,static_cast<unsigned>(b.size()));u32(b,32,static_cast<unsigned>(b.size()-bitmap));
    std::uint32_t crc=0xffffffffU;
    for (auto v:b) { crc^=v;for (unsigned j=0;j<8;++j) crc=(crc>>1)^((crc&1) ? 0xedb88320U : 0); }
    u32(b,96,~crc);
    assert(a.font.load(b.data(),b.size())==FontResourceStatus::Loaded);return a;
}
std::uint64_t selected(const TextSystem& system,TextFontRequest request={}) {
    TextRunMetrics m;assert(system.measure("A",request,m)==S::Ok);return m.primary_resource_id;
}
unsigned expected_weight(const std::vector<unsigned>& weights,unsigned wanted) {
    auto has=[&](unsigned w) { for (auto v:weights) if (v==w) return true;return false; };
    if (wanted<400) {
        for (unsigned w=wanted;w>0;--w) if (has(w)) return w;
        for (unsigned w=wanted+1;w<=1000;++w) if (has(w)) return w;
    } else if (wanted>500) {
        for (unsigned w=wanted;w<=1000;++w) if (has(w)) return w;
        for (unsigned w=wanted-1;w>0;--w) if (has(w)) return w;
    } else {
        for (unsigned w=wanted;w<=500;++w) if (has(w)) return w;
        for (unsigned w=wanted-1;w>0;--w) if (has(w)) return w;
        for (unsigned w=501;w<=1000;++w) if (has(w)) return w;
    }
    assert(false);return 0;
}
void weights() {
    const std::array<unsigned,9> available={100,200,300,400,450,500,600,800,900};
    std::array<Asset,9> assets;
    for (std::size_t i=0;i<assets.size();++i) assets[i]=asset(available[i],16,4,static_cast<unsigned>(i+1));
    for (unsigned mask=1;mask<(1U<<available.size());++mask) {
        std::vector<TextFontFace> faces;std::vector<unsigned> present;
        for (std::size_t i=0;i<available.size();++i) if (mask&(1U<<i)) {
            faces.push_back({available[i],0,assets[i].font});present.push_back(available[i]);
        }
        TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
        for (auto wanted:{1U,100U,299U,399U,400U,401U,450U,499U,500U,501U,650U,999U,1000U}) {
            TextFontRequest request;request.weight=static_cast<std::uint16_t>(wanted);
            assert(selected(system,request)==expected_weight(present,wanted));
        }
    }
}
void strikes_and_families() {
    auto small=asset(400,16,4),large=asset(400,24,4),precise=asset(400,24,8);
    auto named=asset(500,16,4,2);
    std::array<TextFontFace,4> faces={{{1,0,small.font},{2,0,large.font},{3,0,precise.font},
                                      {4,17,named.font,0x12345678U}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    std::uint32_t family_id=999;
    assert(system.resolve_family_hash(0,family_id) && family_id==0);
    assert(system.resolve_family_hash(0x12345678U,family_id) && family_id==17);
    family_id=999;assert(!system.resolve_family_hash(0x87654321U,family_id) && family_id==999);
    TextFontRequest r;r.size_q1000=20000;assert(selected(system,r)==3); // Larger on size tie, then precision.
    r.size_q1000=19999;assert(selected(system,r)==1);
    r.size_q1000=24000;assert(selected(system,r)==3);
    std::array<std::uint32_t,3> families={99,17,17};r.families=families.data();r.family_count=families.size();
    assert(selected(system,r)==4); // Family priority beats a closer default weight.
    std::array<std::uint32_t,2> defaults_first={0,17};r.families=defaults_first.data();r.family_count=2;
    assert(selected(system,r)==3);
    auto duplicate=faces;duplicate[2].resource_id=1;
    assert(system.bind(duplicate.data(),duplicate.size())==S::InvalidFontSet);assert(selected(system)==1);
    std::array<TextFontFace,5> conflicting_alias={{{1,0,small.font},{2,0,large.font},{3,0,precise.font},
                                                    {4,17,named.font,0x12345678U},
                                                    {5,18,named.font,0x12345678U}}};
    assert(system.bind(conflicting_alias.data(),conflicting_alias.size())==S::InvalidFontSet);
    TextFontFace no_default{8,17,named.font};
    assert(system.bind(&no_default,1)==S::InvalidFontSet);
    auto inconsistent=asset(400,24,4,1,{{65,500}},0,801);
    std::array<TextFontFace,2> wrong={{{1,0,small.font},{9,0,inconsistent.font}}};
    assert(system.bind(wrong.data(),wrong.size())==S::InvalidFontSet);
    auto stable=faces;stable[1].resource=large.font;stable[1].resource_id=10;stable[1].family_id=5;
    assert(system.bind(stable.data(),stable.size())==S::Ok);
    assert(selected(system)==1); // Out-of-order registrations do not create unnamed fallback.
}
void fallback_and_geometry() {
    auto primary=asset(400,16,4,1,{{63,400},{65,500},{66,600}},63);
    auto default_font=asset(400,16,4,2,{{0x4e2d,1000}},0,900);
    std::array<TextFontFace,2> faces={{{11,17,primary.font},{22,0,default_font.font}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    const std::uint32_t family=17;TextFontRequest r;r.families=&family;r.family_count=1;
    std::array<PreparedTextGlyph,4> output{};TextRunMetrics measured,prepared;
    const auto before=allocations;
    assert(system.measure("A\xe4\xb8\xad" "B",r,measured)==S::Ok);
    assert(system.prepare("A\xe4\xb8\xad" "B",r,output.data(),output.size(),prepared)==S::Ok);
    assert(allocations==before);
    assert(prepared.primary_resource_id==11 && prepared.glyph_count==3 && prepared.substitutions==0);
    assert(prepared.advance==33600 && prepared.advance==measured.advance);
    assert(prepared.primary_font.ascent==12800 && prepared.ascent==14400 && prepared.descent==3200);
    assert(prepared.natural_height==18400 && prepared.has_ink);
    assert(prepared.ink_left==-1000 && prepared.ink_right==24000 && prepared.ink_top==-2000 && prepared.ink_bottom==0);
    assert(output[0].resource_id==11 && output[1].resource_id==22 && output[2].resource_id==11);
    assert(output[0].source_begin==0 && output[0].source_end==1 && output[0].x==0);
    assert(output[1].source_begin==1 && output[1].source_end==4 && output[1].x==8000);
    assert(output[2].source_begin==4 && output[2].source_end==5 && output[2].x==24000);
    assert(system.prepare("Z",r,output.data(),output.size(),prepared)==S::Ok);
    assert(prepared.substitutions==1 && output[0].codepoint=='?' && output[0].source_codepoint=='Z');
    assert(output[0].resource_id==11 && prepared.advance==6400);
    // A nonselected weight in the first family cannot replace real default coverage.
    auto heavier=asset(500,16,4,3,{{0x4e2d,700}});
    std::array<TextFontFace,3> more={{{11,17,primary.font},{33,17,heavier.font},{22,0,default_font.font}}};
    assert(system.bind(more.data(),more.size())==S::Ok);
    assert(system.prepare("\xe4\xb8\xad",r,output.data(),output.size(),prepared)==S::Ok);
    assert(output[0].resource_id==22);
}
void refusals_and_budgets() {
    auto a=asset();TextFontFace face{1,0,a.font};TextSystem system;TextRunMetrics m;TextFontRequest r;
    std::array<PreparedTextGlyph,4> out{};for (auto& g:out) g.resource_id=999;
    assert(system.measure("A",r,m)==S::NotReady);
    assert(system.bind(&face,1)==S::Ok);
    auto reject=[&](std::string_view text,S wanted) {
        m.advance=123;assert(system.prepare(text,r,out.data(),out.size(),m)==wanted);
        assert(m.advance==0 && m.glyph_count==0 && !m.primary_resource_id);
        for (const auto& g:out) assert(g.resource_id==999);
    };
    reject("ABZ",S::MissingGlyph);
    for (auto invalid:{"\xc0\x80","A\xed\xa0\x80","A\xf4\x90\x80\x80","A\xe4\xb8"}) reject(invalid,S::InvalidUtf8);
    for (auto unsupported:{"A\xcc\x81","A\xe2\x80\x8d","\xd8\xa7","\xf0\x9f\x98\x80","A\xe3\x80\xaa","A\xef\xb8\x8f","A\t","A\n"})
        reject(unsupported,S::UnsupportedText);
    reject(std::string_view("A\0B",3),S::UnsupportedText);
    r.style=TextFaceStyle::Italic;reject("A",S::UnsupportedStyle);r.style=TextFaceStyle::Normal;
    r.size_q1000=-1;reject("A",S::InvalidRequest);r.size_q1000=16000;
    r.weight=0;reject("A",S::InvalidRequest);r.weight=1001;reject("A",S::InvalidRequest);r.weight=400;
    r.family_count=1;reject("A",S::InvalidRequest);r.family_count=0;
    assert(system.prepare("AB",r,out.data(),1,m)==S::OutputTooSmall);
    assert(system.prepare("A",r,nullptr,4,m)==S::OutputTooSmall);
    assert(system.prepare("",r,nullptr,0,m)==S::Ok && m.primary_resource_id==1 && !m.glyph_count);
    assert(system.prepare(" ",r,out.data(),out.size(),m)==S::Ok && !m.has_ink && m.advance==4000);
    for (auto& g:out) g.resource_id=999;
    r.size_q1000=std::numeric_limits<std::int32_t>::max();reject("AA",S::Overflow);r.size_q1000=16000;
    TextSystemLimits limits;limits.max_glyphs=2;assert(system.bind(&face,1,limits)==S::Ok);reject("AAA",S::ResourceLimit);
    limits={};limits.max_text_bytes=2;assert(system.bind(&face,1,limits)==S::Ok);reject("AAA",S::ResourceLimit);
    limits={};limits.max_families=0;assert(system.bind(&face,1,limits)==S::Ok);
    const std::uint32_t family=0;r.families=&family;r.family_count=1;reject("A",S::ResourceLimit);r={};
    limits={};limits.max_work=10;assert(system.bind(&face,1,limits)==S::Ok);
    assert(system.measure("A",r,m)==S::Ok);reject("A",S::ResourceLimit); // Two-pass cost is reserved before writing.
    limits={};limits.max_work=0;assert(system.bind(&face,1,limits)==S::ResourceLimit);
    assert(system.measure("A",r,m)==S::Ok); // Failed rebind preserved previous budget/set.
    assert(system.bind(&face,65)==S::ResourceLimit);
    assert(system.bind(nullptr,0)==S::InvalidFontSet);
    assert(system.bind(&face,1)==S::Ok);r.size_q1000=0;
    assert(system.prepare("A",r,out.data(),out.size(),m)==S::Ok && !m.has_ink && !m.advance);
    auto zero=asset(400,16,4,4,{{65,0}});TextFontFace zero_face{7,0,zero.font};
    assert(system.bind(&zero_face,1)==S::Ok);r={};
    assert(system.prepare("AA",r,out.data(),out.size(),m)==S::Ok && m.has_ink && !m.advance && out[1].x==0);
    system.clear();assert(!system.ready() && system.measure("A",r,m)==S::NotReady);
}
void prepared_packet_paint() {
    auto a=asset();TextFontFace face{0x123400000056ULL,0,a.font};TextSystem system;
    assert(system.bind(&face,1)==S::Ok);
    std::array<PreparedTextGlyph,4> glyphs{};TextRunMetrics metrics;TextFontRequest request;
    assert(system.prepare("AB",request,glyphs.data(),glyphs.size(),metrics)==S::Ok);
    std::string packet;
    assert(encode_prepared_text(glyphs.data(),metrics.glyph_count,request.size_q1000,2,packet));
    PreparedTextView view;
    assert(decode_prepared_text(packet,view) && view.size()==2 && view.source_bytes()==2);

    const Color background{39,59,83,117},ink{213,91,37,255};
    FrameBuffer full(32,20,background);
    const auto before=allocations;
    assert(system.paint(view,full,{0,0,32,20},7000,12000,ink)==S::Ok);
    assert(allocations==before);
    assert(full.pixel(6,10).r==ink.r && full.pixel(6,10).g==ink.g && full.pixel(6,10).b==ink.b);
    assert(full.pixel(14,10).r==ink.r && full.pixel(14,10).g==ink.g && full.pixel(14,10).b==ink.b);
    assert(full.pixel(0,0).r==background.r && full.pixel(0,0).a==background.a);

    FrameBuffer tiled(32,20,background);
    for (int y=0;y<20;y+=3) for (int x=0;x<32;x+=5)
        assert(system.paint(view,tiled,{x,y,5,3},7000,12000,ink)==S::Ok);
    assert(tiled.pixels.size()==full.pixels.size());
    for (std::size_t i=0;i<full.pixels.size();++i) {
        const auto left=full.pixels[i],right=tiled.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }

    FrameBuffer unchanged(32,20,background);
    const auto reject_without_write=[&](TextPaintLimits limits) {
        assert(system.paint(view,unchanged,{0,0,32,20},7000,12000,ink,limits)==TextSystemStatus::ResourceLimit);
        for (const auto& pixel:unchanged.pixels)
            assert(pixel.r==background.r && pixel.g==background.g && pixel.b==background.b && pixel.a==background.a);
    };
    TextPaintLimits limits;limits.max_pixels=1;reject_without_write(limits);
    limits={};limits.max_work=1;reject_without_write(limits);
    auto malformed=packet;malformed[4]=2;PreparedTextView preserved=view;
    assert(!decode_prepared_text(malformed,preserved) && preserved.size()==view.size());
    assert(system.paint(preserved,unchanged,{0,0,32,20},7000,12000,ink)==TextSystemStatus::Ok);

    DisplayCommand command;
    command.type=DisplayCommandType::PreparedText;command.rect={0,0,32,20};
    command.text=packet;command.color=ink;
    set_prepared_text_origin(command,0,0);
    SoftwareRasterizerOptions raster_options;raster_options.text_system=&system;
    FrameBuffer rendered(32,20,background);
    SoftwareRasterizer({},nullptr,raster_options).rasterize(command,rendered,{0,0,32,20});
    FrameBuffer reference(32,20,background);
    assert(system.paint(view,reference,{0,0,32,20},0,0,ink)==TextSystemStatus::Ok);
    assert(rendered.pixels.size()==reference.pixels.size());
    for (std::size_t i=0;i<reference.pixels.size();++i) {
        const auto left=reference.pixels[i],right=rendered.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }
    LayerNode root;root.bounds={0,0,32,20};root.display_list.push_back(command);
    SoftwareCompositor::Options compositor_options;compositor_options.text_system=&system;
    const auto composited=SoftwareCompositor({},compositor_options).render(root,32,20,background);
    for (std::size_t i=0;i<reference.pixels.size();++i) {
        const auto left=reference.pixels[i],right=composited.pixels[i];
        assert(left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a);
    }
}
const LayoutBox* find_text_box(const LayoutBox& box);
void html_layout_to_core_paint() {
    auto a=asset();
    const auto family_hash=normalized_font_family_hash("Core Test");
    std::array<TextFontFace,2> faces={{{101,17,a.font,family_hash},{102,0,a.font,0}}};
    TextSystem system;
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    HtmlParser html;CssParser css;
    auto document=html.parse("<body><div>AB</div></body>");
    StyleResolver resolver(css.parse("div { font-family: 'Core Test'; font-size: 16px; white-space: nowrap; }"));
    LayoutEngineOptions layout_options;layout_options.text_system=&system;
    auto layout=LayoutEngine(resolver,{},layout_options).layout(*document,80,40);
    const LayoutBox* text_box=nullptr;
    const auto find_text=[&](const auto& self,const LayoutBox& box)->void {
        if (box.node && box.node->type==NodeType::Text) { text_box=&box;return; }
        for (const auto& child:box.children) if (!text_box) self(self,*child);
    };
    find_text(find_text,*layout);
    assert(text_box && text_box->text_layout_cache.prepared_text);
    assert(text_box->text_layout_cache.prepared_packet.substr(0,4)=="JFTP");
    assert(text_box->text_layout_cache.prepared_origin_x==text_box->rect.x);
    assert(text_box->text_layout_cache.prepared_baseline_y>text_box->rect.y);

    LayerTreeBuilderOptions layer_options;layer_options.text_system=&system;
    auto layers=LayerTreeBuilder(layer_options).build(*layout);
    bool found_prepared=false;
    for (const auto& command:layers->display_list)
        found_prepared|=command.type==DisplayCommandType::PreparedText;
    assert(found_prepared);
    SoftwareCompositor::Options compositor_options;compositor_options.text_system=&system;
    const auto frame=SoftwareCompositor({},compositor_options).render(*layers,80,40,Color{255,255,255,255});
    std::size_t ink=0;
    for (const auto pixel:frame.pixels) ink+=pixel.r<255 || pixel.g<255 || pixel.b<255;
    assert(ink>0);

    auto positioned_document=html.parse("<body><div>AB</div></body>");
    StyleResolver positioned_resolver(css.parse(
        "div { position: absolute; left: 20px; top: 10px; font-family: 'Core Test'; white-space: nowrap; }"));
    auto positioned_layout=LayoutEngine(positioned_resolver,{},layout_options).layout(*positioned_document,80,40);
    const LayoutBox* positioned_text=find_text_box(*positioned_layout);
    assert(positioned_text && positioned_text->text_layout_cache.prepared_text);
    assert(positioned_text->rect.x>=20 && positioned_text->rect.y>=10);
    assert(positioned_text->text_layout_cache.prepared_origin_x==positioned_text->rect.x);
    LayerTreeBuilder positioned_builder(layer_options);
    auto positioned_layers=positioned_builder.build(*positioned_layout);
    bool found_positioned_text=false;
    const auto check_positioned_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list) {
            if (command.type!=DisplayCommandType::PreparedText) continue;
            found_positioned_text=true;
            assert(prepared_text_origin_x(command)==positioned_text->rect.x);
            assert(prepared_text_baseline_y(command)==positioned_text->text_layout_cache.prepared_baseline_y);
        }
        for (const auto& child:layer.children) self(self,*child);
    };
    check_positioned_commands(check_positioned_commands,*positioned_layers);
    assert(found_positioned_text);

    auto wrapped_document=html.parse("<body><div>A B</div></body>");
    StyleResolver wrapped_resolver(css.parse(
        "div { width: 10px; font-family: 'Core Test'; font-size: 16px; }"));
    auto wrapped_layout=LayoutEngine(wrapped_resolver,{},layout_options).layout(*wrapped_document,80,40);
    const LayoutBox* wrapped_text=find_text_box(*wrapped_layout);
    assert(wrapped_text && wrapped_text->text_layout_cache.prepared_text);
    assert((wrapped_text->text_layout_cache.lines==std::vector<std::string>{"A","B"}));
    assert(wrapped_text->text_layout_cache.prepared_lines.size()==2);
    auto wrapped_layers=LayerTreeBuilder(layer_options).build(*wrapped_layout);
    std::size_t wrapped_commands=0;
    const auto count_wrapped_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list)
            wrapped_commands+=command.type==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) self(self,*child);
    };
    count_wrapped_commands(count_wrapped_commands,*wrapped_layers);
    assert(wrapped_commands==2);

    auto anywhere_document=html.parse("<body><div>AB</div></body>");
    StyleResolver anywhere_resolver(css.parse(
        "div { width: 4px; font-family: 'Core Test'; overflow-wrap: anywhere; }"));
    auto anywhere_layout=LayoutEngine(anywhere_resolver,{},layout_options).layout(*anywhere_document,80,40);
    const LayoutBox* anywhere_text=find_text_box(*anywhere_layout);
    assert(anywhere_text && anywhere_text->text_layout_cache.prepared_text);
    assert((anywhere_text->text_layout_cache.lines==std::vector<std::string>{"A","B"}));

    const auto cached_generation=wrapped_text->text_layout_cache.text_system_generation;
    assert(system.bind(faces.data(),faces.size())==S::Ok);
    assert(system.generation()>cached_generation);
    auto stale_layers=LayerTreeBuilder(layer_options).build(*wrapped_layout);
    std::size_t stale_prepared_commands=0;
    const auto count_stale_commands=[&](const auto& self,const LayerNode& layer)->void {
        for (const auto& command:layer.display_list)
            stale_prepared_commands+=command.type==DisplayCommandType::PreparedText;
        for (const auto& child:layer.children) self(self,*child);
    };
    count_stale_commands(count_stale_commands,*stale_layers);
    assert(stale_prepared_commands==0);
}
bool forbidden_host_measure(const std::string&,int,int,TextMetrics*,void* context) {
    ++*static_cast<unsigned*>(context);
    return false;
}
const LayoutBox* find_text_box(const LayoutBox& box) {
    if (box.node && box.node->type==NodeType::Text) return &box;
    for (const auto& child:box.children) {
        if (const auto* found=find_text_box(*child)) return found;
    }
    return nullptr;
}
void core_layout_refusals_do_not_fall_back_to_host_text() {
    auto a=asset();
    std::array<TextFontFace,2> faces={{{101,17,a.font,normalized_font_family_hash("Core Test")},
                                      {102,0,a.font,0}}};
    TextSystem system;assert(system.bind(faces.data(),faces.size())==S::Ok);
    unsigned host_calls=0;
    TextMeasureProvider host_measure{forbidden_host_measure,&host_calls};
    HtmlParser html;CssParser css;
    const auto run_refusal=[&](const char* markup,const char* rules,LayoutEngineOptions options) {
        VectorDiagnosticSink diagnostics;
        options.text_system=&system;
        options.diagnostics=&diagnostics;
        auto document=html.parse(markup);
        StyleResolver resolver(css.parse(rules));
        auto layout=LayoutEngine(resolver,host_measure,options).layout(*document,80,40);
        const auto* text=find_text_box(*layout);
        assert(text && text->text_layout_cache.valid && !text->text_layout_cache.prepared_text);
        assert(text->rect.width==0 && text->rect.height==0);
        assert(diagnostics.size()==1);
        assert(diagnostics.diagnostics()[0].code=="layout-core-text-refused");
        LayerTreeBuilderOptions layer_options;layer_options.text_system=&system;
        auto layers=LayerTreeBuilder(layer_options).build(*layout);
        for (const auto& command:layers->display_list)
            assert(command.type!=DisplayCommandType::Text && command.type!=DisplayCommandType::PreparedText);
    };

    run_refusal("<body><div>AB</div></body>",
                "div { font-family: 'Unregistered'; white-space: nowrap; }",{});
    LayoutEngineOptions glyph_limit;glyph_limit.max_prepared_text_glyphs=1;
    run_refusal("<body><div>AB</div></body>",
                "div { font-family: 'Core Test'; white-space: nowrap; }",glyph_limit);
    assert(host_calls==0);
}
int inspect(int argc,char** argv) {
    std::vector<Asset> assets(static_cast<std::size_t>(argc-1));
    std::vector<TextFontFace> faces(assets.size());
    for (std::size_t i=0;i<assets.size();++i) {
        std::ifstream stream(argv[i+1],std::ios::binary);if (!stream) return 2;
        assets[i].bytes.assign(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
        if (assets[i].font.load(assets[i].bytes.data(),assets[i].bytes.size())!=FontResourceStatus::Loaded) return 3;
        faces[i]={i+1,0,assets[i].font};
    }
    TextSystem system;if (system.bind(faces.data(),faces.size())!=S::Ok) return 4;
    std::cout<<'[';bool first=true;
    for (auto size:{15500,24000,28000}) for (auto weight:{350,400,450,500,550,700,900}) {
        TextFontRequest r;r.size_q1000=size;r.weight=static_cast<std::uint16_t>(weight);
        std::array<PreparedTextGlyph,16> glyphs;TextRunMetrics m;
        if (system.prepare("A jg\xe2\x80\xa6\xe4\xb8\xad",r,glyphs.data(),glyphs.size(),m)!=S::Ok) return 5;
        if (!first) std::cout<<',';first=false;
        std::cout<<"{\"size\":"<<size<<",\"weight\":"<<weight<<",\"primary\":"<<m.primary_resource_id
            <<",\"font\":["<<m.primary_font.ascent<<','<<m.primary_font.descent<<','<<m.primary_font.line_gap
            <<','<<m.primary_font.line_height<<"],\"advance\":"<<m.advance<<",\"glyphs\":[";
        for (std::size_t i=0;i<m.glyph_count;++i) {
            if (i) std::cout<<',';const auto& g=glyphs[i];
            std::cout<<'['<<g.resource_id<<','<<g.codepoint<<','<<g.glyph_id<<','<<g.source_begin<<','<<g.source_end
                <<','<<g.x<<','<<g.geometry.advance<<','<<g.geometry.bearing_x<<','<<g.geometry.bearing_y
                <<','<<g.geometry.width<<','<<g.geometry.height<<']';
        }
        std::cout<<"]}";
    }
    std::cout<<"]\n";return 0;
}
} // namespace
int main(int argc,char** argv) {
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    if (argc>1) return inspect(argc,argv);
    weights();strikes_and_families();fallback_and_geometry();refusals_and_budgets();
    prepared_packet_paint();html_layout_to_core_paint();core_layout_refusals_do_not_fall_back_to_host_text();
    std::cout<<"text system selection/geometry/transaction/budget tests passed\n";
}
