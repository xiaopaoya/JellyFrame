#pragma once
#include "app_runtime/runtime_font_catalog.h"
#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
#include <algorithm>
#include <cassert>
#include <vector>
namespace jellyframe::runtime_test_fonts {
inline void u16(std::vector<std::uint8_t>& b,std::size_t at,unsigned value){
    b[at]=static_cast<std::uint8_t>(value);b[at+1]=static_cast<std::uint8_t>(value>>8);
}
inline void u32(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t value){
    for(unsigned i=0;i<4;++i)b[at+i]=static_cast<std::uint8_t>(value>>(8*i));
}
inline void checksum(std::vector<std::uint8_t>& b){
    u32(b,96,0);std::uint32_t crc=0xffffffffU;
    for(auto byte:b){crc^=byte;for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320U:0);}
    u32(b,96,~crc);
}
inline std::vector<std::uint8_t> font_bytes(unsigned weight=400,unsigned strike=16){
    constexpr unsigned count=4,bitmap=128+count*40;
    std::vector<std::uint8_t> b(bitmap+12,0);const char magic[]="JFFONT2";
    std::copy(magic,magic+8,b.begin());u16(b,8,2);u16(b,10,128);u16(b,12,40);u16(b,14,1);
    u32(b,16,static_cast<unsigned>(b.size()));u32(b,20,count);u32(b,24,128);u32(b,28,bitmap);
    u32(b,32,12);u16(b,36,1000);u16(b,38,strike);u16(b,40,weight);b[42]=8;
    u32(b,44,800);u32(b,48,200);u32(b,52,50);u32(b,56,63);
    b[64]=static_cast<std::uint8_t>(weight/100);u32(b,100,1);u16(b,104,1);
    const unsigned cps[]={32,63,65,66};
    for(unsigned i=0;i<count;++i){
        const auto at=128+i*40;u32(b,at,cps[i]);u32(b,at+4,i+1);u32(b,at+8,i?500+i*50:250);
        u32(b,at+28,i?4*(i-1):0);
        if(i){u32(b,at+12,static_cast<unsigned>(-1));u32(b,at+16,static_cast<unsigned>(-2));
            u16(b,at+20,2);u16(b,at+22,2);u32(b,at+24,2);u32(b,at+32,4);}
    }
    for(unsigned i=0;i<12;++i)b[bitmap+i]=static_cast<std::uint8_t>(i%3?255:96);
    checksum(b);return b;
}
inline RuntimeFontResource::Handle resource(unsigned weight=400,unsigned strike=16){
    auto bytes=font_bytes(weight,strike);RuntimeFontResource::Handle result;
    assert(RuntimeFontResource::copy(bytes.data(),bytes.size(),result)==RuntimeFontStatus::Ready);return result;
}
struct Fonts{
    RuntimeFontCatalog::Handle catalog;
    Fonts(){RuntimeFontRegistration faces[]={{resource(),0},{resource(700,24),0}};
        assert(RuntimeFontCatalog::create(faces,2,catalog)==RuntimeFontStatus::Ready);}
};
} // namespace jellyframe::runtime_test_fonts
#endif
