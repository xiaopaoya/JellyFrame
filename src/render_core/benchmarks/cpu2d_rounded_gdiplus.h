#pragma once

#include "cpu2d_rounded_fixture.h"

#include <objidl.h>
#include <gdiplus.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace jellyframe::benchmark {

inline void check_gdiplus(Gdiplus::Status status) {
    if (status != Gdiplus::Ok) throw std::runtime_error("GDI+ rounded qualification failed: " + std::to_string(status));
}

class GdiPlusSession {
public:
    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput input;
        check_gdiplus(Gdiplus::GdiplusStartup(&token_, &input, nullptr));
    }
    ~GdiPlusSession() { Gdiplus::GdiplusShutdown(token_); }
    GdiPlusSession(const GdiPlusSession&) = delete;
    GdiPlusSession& operator=(const GdiPlusSession&) = delete;

private:
    ULONG_PTR token_ = 0;
};

// Qualification only: native binary path at 4x resolution followed by box
// reduction. The source mask is produced by GDI+, never by the Core oracle.
inline std::vector<int> gdiplus_rounded_coverage(const RoundedFixture& fixture, int width, int height) {
    constexpr int scale = 4;
    const int stride = width * scale * 4;
    std::vector<std::uint8_t> storage(static_cast<std::size_t>(stride) * height * scale, 0);
    {
        Gdiplus::Bitmap bitmap(width * scale, height * scale, stride, PixelFormat32bppARGB, storage.data());
        check_gdiplus(bitmap.GetLastStatus());
        Gdiplus::Graphics graphics(&bitmap);
        check_gdiplus(graphics.GetLastStatus());
        check_gdiplus(graphics.SetPageUnit(Gdiplus::UnitPixel));
        check_gdiplus(graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone));
        check_gdiplus(graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone));
        check_gdiplus(graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy));
        check_gdiplus(graphics.Clear(Gdiplus::Color(255, 0, 0, 0)));
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        check_gdiplus(white.GetLastStatus());
        const auto rect = fixture.rect;
        const auto x = static_cast<Gdiplus::REAL>(rect.x * scale);
        const auto y = static_cast<Gdiplus::REAL>(rect.y * scale);
        const auto w = static_cast<Gdiplus::REAL>(rect.width * scale);
        const auto h = static_cast<Gdiplus::REAL>(rect.height * scale);
        const auto diameter = static_cast<Gdiplus::REAL>(fixture.radius * scale * 2);
        Gdiplus::GraphicsPath path;
        check_gdiplus(path.GetLastStatus());
        if (fixture.radius == 0) {
            check_gdiplus(path.AddRectangle(Gdiplus::RectF(x, y, w, h)));
        } else {
            check_gdiplus(path.AddArc(x, y, diameter, diameter, 180.0F, 90.0F));
            check_gdiplus(path.AddArc(x + w - diameter, y, diameter, diameter, 270.0F, 90.0F));
            check_gdiplus(path.AddArc(x + w - diameter, y + h - diameter, diameter, diameter, 0.0F, 90.0F));
            check_gdiplus(path.AddArc(x, y + h - diameter, diameter, diameter, 90.0F, 90.0F));
            check_gdiplus(path.CloseFigure());
        }
        check_gdiplus(graphics.FillPath(&white, &path));
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    std::vector<int> coverage(static_cast<std::size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int covered = 0;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    const auto offset = static_cast<std::size_t>(y * scale + sy) * stride + (x * scale + sx) * 4;
                    const auto value = storage[offset];
                    if ((value != 0 && value != 255) || storage[offset + 1] != value || storage[offset + 2] != value) {
                        throw std::runtime_error("GDI+ binary mask contains nonbinary RGB");
                    }
                    covered += value == 255 ? 1 : 0;
                }
            }
            coverage[static_cast<std::size_t>(y) * width + x] = (covered * 255 + 8) / 16;
        }
    }
    return coverage;
}

} // namespace jellyframe::benchmark
