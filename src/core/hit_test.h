#pragma once

#include "core/geometry.h"
#include "core/layer_tree.h"
#include "core/layout.h"

namespace wearweb {

struct HitTestResult {
    const Node* node = nullptr;
    const LayoutBox* box = nullptr;
    int local_x = 0;
    int local_y = 0;

    explicit operator bool() const {
        return node != nullptr && box != nullptr;
    }
};

class HitTester {
public:
    HitTestResult hit_test(const LayoutBox& root, int x, int y) const;
    HitTestResult hit_test(const LayerNode& root, int x, int y) const;

private:
    HitTestResult hit_test_box(const LayoutBox& box, int x, int y) const;
    HitTestResult hit_test_layer(const LayerNode& layer, int x, int y, Rect clip, bool has_clip) const;
};

} // namespace wearweb
