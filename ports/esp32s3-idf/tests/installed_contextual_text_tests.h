#pragma once

#include <iostream>
#include "render_core/feature_config.h"

#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API && \
    defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
namespace {
using namespace jellyframe;
using namespace jellyframe_esp32s3;

static bool same_pixels(const FrameBuffer& left, const FrameBuffer& right) {
    if (left.pixels.size() != right.pixels.size()) return false;
    for (std::size_t i = 0; i < left.pixels.size(); ++i) {
        const auto a = left.pixels[i];
        const auto b = right.pixels[i];
        if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) return false;
    }
    return true;
}

static ScriptTaskWorkerRuntimeOptions contextual_flex_options(
    const RuntimeFontCatalog::Handle& catalog,
    const RuntimeShapedTextBinding::Handle& binding) {
    ScriptTaskWorkerRuntimeOptions options;
    options.viewport = {0, 0, 160, 64};
    options.frame_codec = {128, 16384, 16, 32768};
    options.frame_codec.version = 12;
    options.frame_codec.max_groups = 16;
    options.text_catalog = catalog;
    options.shaped_text_binding = binding;
    return options;
}

static const std::string contextual_flex_html =
    "<body><div id='row'><div id='first'>A</div><div id='second'>B</div>"
    "<div id='third'>C</div></div></body>";
static const std::string contextual_flex_css =
    "*{margin:0;padding:0;font-weight:400}"
    "body{background:#ffffff}"
    "#row{display:flex;flex-direction:row;flex-wrap:nowrap;width:120px;height:28px}"
    "#first,#second,#third{width:40px;height:28px;font-size:16px;line-height:20px;color:#000000}"
    "#first{background:#e02020}#second{background:#20c040}#third{background:#2040e0}";

static const std::string contextual_flex_column_css =
    "*{margin:0;padding:0;font-weight:400}"
    "body{background:#ffffff}"
    "#column{display:flex;flex-direction:column;flex-wrap:nowrap;width:40px;height:84px}"
    "#first,#second,#third{width:40px;height:28px;font-size:16px;line-height:20px;color:#000000}"
    "#first{background:#e02020}#second{background:#20c040}#third{background:#2040e0}";
static const std::string contextual_flex_column_html =
    "<body><div id='column'><div id='first'>A</div><div id='second'>B</div>"
    "<div id='third'>C</div></div></body>";

static void installed_contextual_flex_column_worker_test() {
    runtime_test_shaped_text::Assets assets("regular", 7602);
    const auto binding = runtime_test_shaped_text::create_binding(assets);
    const runtime_test_fonts::Fonts fonts;
    auto supervisor = ScriptTaskSupervisor{};
    const auto session = supervisor.begin(762, fonts.catalog, {}, binding);
    auto options = contextual_flex_options(fonts.catalog, binding);
    options.viewport = {0, 0, 160, 96};
    ScriptTaskWorkerRuntime worker(session, options);
#if !JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    assert(worker.initialize(supervisor, contextual_flex_column_html, contextual_flex_column_css) !=
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(!worker.publish_frame(supervisor).accepted());
    return;
#else
    assert(worker.initialize(supervisor, contextual_flex_column_html, contextual_flex_column_css) ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(worker.publish_frame(supervisor).accepted());
    ScriptTaskAcceptedAppFrame accepted;
    assert(take_installed_core_text_frame(supervisor, session, options.frame_codec, accepted) ==
        ScriptTaskAppFrameTakeStatus::Accepted);
    assert(accepted.shaped_text_binding() == binding);
    assert(accepted.frame().shaped_text.size() == 3);
    for (const auto& text : accepted.frame().shaped_text) assert(text.baseline_x_q1000 == 0);
    assert(accepted.frame().shaped_text[0].baseline_y_q1000 == 16000);
    assert(accepted.frame().shaped_text[1].baseline_y_q1000 == 44000);
    assert(accepted.frame().shaped_text[2].baseline_y_q1000 == 72000);

    auto document = HtmlParser{}.parse(contextual_flex_column_html);
    StyleResolver styles(CssParser{}.parse(contextual_flex_column_css));
    auto render_tree = RenderTreeBuilder(styles).build(*document);
    LayoutEngine engine(styles);
    runtime_shaped::LayoutOptions layout_options;
    layout_options.binding = binding->core_binding();
    std::uint64_t work = binding->work_limit();
    runtime_shaped::LayoutResult layout;
    assert(engine.layout_contextual(*render_tree, 160, 96, layout_options, work, layout) ==
        runtime_shaped::Status::ok);
    assert(layout.valid());
    const auto find_box = [](const LayoutBox* root, const char* id) {
        const LayoutBox* found = nullptr;
        const auto visit = [&](const auto& self, const LayoutBox* box) -> void {
            if (box == nullptr || found != nullptr) return;
            if (box->node != nullptr && box->node->attribute("id") == id) {
                found = box;
                return;
            }
            for (const auto& child : box->children) self(self, child.get());
        };
        visit(visit, root);
        return found;
    };
    const auto* column = find_box(layout.tree(), "column");
    const auto* first = find_box(layout.tree(), "first");
    const auto* second = find_box(layout.tree(), "second");
    const auto* third = find_box(layout.tree(), "third");
    assert(column != nullptr && column->rect.x == 0 && column->rect.y == 0 &&
        column->rect.width == 40 && column->rect.height == 84);
    assert(first != nullptr && first->rect.x == 0 && first->rect.y == 0 &&
        first->rect.width == 40 && first->rect.height == 28);
    assert(second != nullptr && second->rect.x == 0 && second->rect.y == 28 &&
        second->rect.width == 40 && second->rect.height == 28);
    assert(third != nullptr && third->rect.x == 0 && third->rect.y == 56 &&
        third->rect.width == 40 && third->rect.height == 28);

    ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.shaped_text_binding = binding;
    ScriptTaskFrameRenderStatus render_status;
    const auto pixels = ScriptTaskFrameRenderer(renderer_options).render(
        accepted.frame(), {255,255,255,255}, &render_status);
    assert(render_status == ScriptTaskFrameRenderStatus::Accepted);
    assert(pixels.pixels[10 * pixels.width + 10].r == 224 &&
        pixels.pixels[10 * pixels.width + 10].g == 32 &&
        pixels.pixels[10 * pixels.width + 10].b == 32);
    assert(pixels.pixels[38 * pixels.width + 10].r == 32 &&
        pixels.pixels[38 * pixels.width + 10].g == 192 &&
        pixels.pixels[38 * pixels.width + 10].b == 64);
    assert(pixels.pixels[66 * pixels.width + 10].r == 32 &&
        pixels.pixels[66 * pixels.width + 10].g == 64 &&
        pixels.pixels[66 * pixels.width + 10].b == 224);
    const auto retained_pixels = pixels;

    ScriptTaskSupervisor rejected_supervisor;
    const auto rejected_session = rejected_supervisor.begin(763, fonts.catalog, {}, binding);
    auto rejected_options = contextual_flex_options(fonts.catalog, binding);
    ScriptTaskWorkerRuntime rejected_worker(rejected_session, rejected_options);
    assert(rejected_worker.initialize(rejected_supervisor, contextual_flex_column_html,
        contextual_flex_column_css + "#column{height:auto}") !=
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(!rejected_worker.publish_frame(rejected_supervisor).accepted());

    worker.stop();
    assert(supervisor.begin_teardown(session).session == session);
    assert(supervisor.complete_teardown(session).session == session);
    ScriptTaskFrameRenderStatus replay_status;
    const auto replay = ScriptTaskFrameRenderer(renderer_options).render(
        accepted.frame(), {255,255,255,255}, &replay_status);
    assert(replay_status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(retained_pixels, replay));
#endif
}

static void installed_contextual_flex_row_worker_test() {
    runtime_test_shaped_text::Assets assets("regular", 7601);
    const auto binding = runtime_test_shaped_text::create_binding(assets);
    const runtime_test_fonts::Fonts fonts;

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto supervisor = ScriptTaskSupervisor{};
    const auto session = supervisor.begin(760, fonts.catalog, {}, binding);
    auto options = contextual_flex_options(fonts.catalog, binding);
    ScriptTaskWorkerRuntime worker(session, options);
    assert(worker.initialize(supervisor, contextual_flex_html, contextual_flex_css) ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(worker.publish_frame(supervisor).accepted());
    ScriptTaskAcceptedAppFrame accepted;
    assert(take_installed_core_text_frame(supervisor, session, options.frame_codec, accepted) ==
        ScriptTaskAppFrameTakeStatus::Accepted);
    assert(accepted.shaped_text_binding() == binding);
    assert(accepted.frame().shaped_text.size() == 3);
    assert(accepted.frame().shaped_text[0].baseline_x_q1000 == 0);
    assert(accepted.frame().shaped_text[1].baseline_x_q1000 == 40000);
    assert(accepted.frame().shaped_text[2].baseline_x_q1000 == 80000);
    for (const auto& command : accepted.frame().display_list) {
        const auto type = detail::command_type(command);
        assert(type != DisplayCommandType::Text);
        assert(!detail::is_prepared_command_type(type));
        assert(!detail::has_prepared_text_packet(command));
    }

    auto document = HtmlParser{}.parse(contextual_flex_html);
    StyleResolver styles(CssParser{}.parse(contextual_flex_css));
    auto render_tree = RenderTreeBuilder(styles).build(*document);
    LayoutEngine engine(styles);
    runtime_shaped::LayoutOptions layout_options;
    layout_options.binding = binding->core_binding();
    std::uint64_t work = binding->work_limit();
    runtime_shaped::LayoutResult layout;
    assert(engine.layout_contextual(*render_tree, 160, 64, layout_options, work, layout) ==
        runtime_shaped::Status::ok);
    assert(layout.valid());

    const auto find_box = [](const LayoutBox* root, const char* id) {
        const LayoutBox* found = nullptr;
        const auto visit = [&](const auto& self, const LayoutBox* box) -> void {
            if (box == nullptr || found != nullptr) return;
            if (box->node != nullptr && box->node->attribute("id") == id) {
                found = box;
                return;
            }
            for (const auto& child : box->children) self(self, child.get());
        };
        visit(visit, root);
        return found;
    };
    const auto* row = find_box(layout.tree(), "row");
    const auto* first = find_box(layout.tree(), "first");
    const auto* second = find_box(layout.tree(), "second");
    const auto* third = find_box(layout.tree(), "third");
    assert(row != nullptr && row->rect.x == 0 && row->rect.y == 0 &&
        row->rect.width == 120 && row->rect.height == 28);
    assert(first != nullptr && first->rect.x == 0 && first->rect.y == 0 &&
        first->rect.width == 40 && first->rect.height == 28);
    assert(second != nullptr && second->rect.x == 40 && second->rect.y == 0 &&
        second->rect.width == 40 && second->rect.height == 28);
    assert(third != nullptr && third->rect.x == 80 && third->rect.y == 0 &&
        third->rect.width == 40 && third->rect.height == 28);

    ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.shaped_text_binding = binding;
    ScriptTaskFrameRenderer renderer(renderer_options);
    ScriptTaskFrameRenderStatus render_status;
    auto pixels = renderer.render(accepted.frame(), {255,255,255,255}, &render_status);
    assert(render_status == ScriptTaskFrameRenderStatus::Accepted);
    assert(pixels.pixels[26 * pixels.width + 10].r == 224 &&
        pixels.pixels[26 * pixels.width + 10].g == 32 &&
        pixels.pixels[26 * pixels.width + 10].b == 32);
    assert(pixels.pixels[26 * pixels.width + 50].r == 32 &&
        pixels.pixels[26 * pixels.width + 50].g == 192 &&
        pixels.pixels[26 * pixels.width + 50].b == 64);
    assert(pixels.pixels[26 * pixels.width + 90].r == 32 &&
        pixels.pixels[26 * pixels.width + 90].g == 64 &&
        pixels.pixels[26 * pixels.width + 90].b == 224);
    const auto retained_pixels = pixels;

    for (const auto& rejected_css : {
            contextual_flex_css + "#row{flex-wrap:wrap}",
            contextual_flex_column_css + "#column{height:auto}"}) {
        ScriptTaskSupervisor rejected_supervisor;
        const auto rejected_session = rejected_supervisor.begin(761, fonts.catalog, {}, binding);
        auto rejected_options = contextual_flex_options(fonts.catalog, binding);
        ScriptTaskWorkerRuntime rejected_worker(rejected_session, rejected_options);
        const auto& rejected_html = rejected_css.find("#column") != std::string::npos
            ? contextual_flex_column_html : contextual_flex_html;
        assert(rejected_worker.initialize(rejected_supervisor, rejected_html, rejected_css) !=
            ScriptTaskWorkerRuntimeInitStatus::Accepted);
        assert(!rejected_worker.publish_frame(rejected_supervisor).accepted());
    }

    worker.stop();
    assert(supervisor.begin_teardown(session).session == session);
    assert(supervisor.complete_teardown(session).session == session);
    ScriptTaskFrameRendererOptions replay_options;
    replay_options.shaped_text_binding = binding;
    ScriptTaskFrameRenderStatus replay_status;
    const auto replay = ScriptTaskFrameRenderer(replay_options).render(
        accepted.frame(), {255,255,255,255}, &replay_status);
    assert(replay_status == ScriptTaskFrameRenderStatus::Accepted && same_pixels(retained_pixels, replay));
#else
    auto supervisor = ScriptTaskSupervisor{};
    const auto session = supervisor.begin(760, fonts.catalog, {}, binding);
    auto options = contextual_flex_options(fonts.catalog, binding);
    ScriptTaskWorkerRuntime worker(session, options);
    assert(worker.initialize(supervisor, contextual_flex_html, contextual_flex_css) !=
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(!worker.publish_frame(supervisor).accepted());
#endif
}
} // namespace
#endif

// ABI9 scalar and contextual resources use explicit independent frame routes.
inline int run_installed_contextual_text_tests() {
    using namespace jellyframe;
    using namespace jellyframe_esp32s3;
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(runtime_test_fonts::font_bytes());
    CoreFontResourceView system{bytes->data(), bytes->size()};
    const auto clock = +[]() -> std::int64_t { return 1; };
    InstalledAppResources::Handle resources;
    assert(create_installed_app_resources({}, {}, "/index.html", &system, 1, bytes, clock, resources));
    assert(resources && resources->text_catalog);
    InstalledResourceSnapshot images;
    images.entries.push_back({"/bad.bmp", HostResourceKind::Image, {1,2,3}});
    auto original = resources;
    assert(!create_installed_app_resources(std::move(images), {}, "/index.html", &system, 1, bytes, clock, resources));
    assert(resources == original);
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {4,4096};
    supervisor_options.frame_leases = {4,32768,65536};
    ScriptTaskSupervisor supervisor(supervisor_options);
    assert(!script_task_app_frame_codec_supports_version(10));
    const auto scalar_session = begin_installed_script_session(supervisor,41,resources);
    assert(scalar_session.valid());
    AppInstalledTextPipeline pipeline;
    pipeline.status = AppInstalledTextPipelineStatus::Scalar;
    assert(installed_text_pipeline_supported(pipeline));
    assert(installed_text_pipeline_frame_version(pipeline) == kRuntimeScalarFrameVersion);
    ScriptTaskWorkerRuntimeOptions scalar_options;
    resources->bind_worker_resources(scalar_options);
    scalar_options.viewport = {0,0,64,32};
    scalar_options.frame_codec = {128,16384,16,32768};
    scalar_options.frame_codec.version = installed_text_pipeline_frame_version(pipeline);
    scalar_options.frame_codec.max_groups = 16;
    ScriptTaskAcceptedAppFrame scalar_frame;
    {
        ScriptTaskWorkerRuntime worker(scalar_session, scalar_options);
        assert(worker.initialize(supervisor, "<body><div>AB</div></body>",
            "body{margin:0;background:#fff}div{font-size:16px;color:#000}") ==
            ScriptTaskWorkerRuntimeInitStatus::Accepted);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_installed_core_text_frame(supervisor, scalar_session,
            scalar_options.frame_codec, scalar_frame) == ScriptTaskAppFrameTakeStatus::Accepted);
        worker.stop();
    }
    FrameBuffer scalar_pixels(64,32,{255,255,255,255});
    assert(render_installed_core_text_frame(ScriptTaskFrameRenderer{},scalar_frame,scalar_pixels,
        {255,255,255,255},nullptr,0,nullptr,nullptr));
    assert(std::any_of(scalar_pixels.pixels.begin(), scalar_pixels.pixels.end(), [](Color c){ return c.r < 100; }));
    supervisor.begin_teardown(scalar_session); supervisor.complete_teardown(scalar_session);
    scalar_frame = {}; scalar_options = {};
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    installed_contextual_flex_row_worker_test();
    installed_contextual_flex_column_worker_test();
    runtime_test_shaped_text::Assets assets("regular", 7501);
    auto binding = runtime_test_shaped_text::create_binding(assets);
    auto shaped = std::make_shared<InstalledAppResources>(*resources);
    shaped->shaped_text_binding = binding;
    shaped->image_provider.reset();
    pipeline.status = AppInstalledTextPipelineStatus::Ready;
    pipeline.frame_version = kRuntimeShapedFrameVersion;
    pipeline.binding = binding;
    assert(installed_text_pipeline_supported(pipeline));
    for (auto wrong : {0,10,11}) {
        pipeline.frame_version = static_cast<std::uint8_t>(wrong);
        assert(!installed_text_pipeline_supported(pipeline));
    }
    pipeline.frame_version = 12;
    pipeline.binding.reset();
    assert(!installed_text_pipeline_supported(pipeline));
    const auto session = begin_installed_script_session(supervisor,43,shaped);
    assert(session.valid() && supervisor.shaped_text_binding(session) == binding);
    ScriptTaskWorkerRuntimeOptions options;
    shaped->bind_worker_resources(options);
    assert(options.shaped_text_binding == binding && options.text_catalog == shaped->text_catalog);
    options.viewport = {0,0,120,60};
    options.frame_codec = {128,16384,16,32768};
    options.frame_codec.version = 12;
    options.frame_codec.max_groups = 16;
    ScriptTaskAcceptedAppFrame initial, changed;
    {
        ScriptTaskWorkerRuntime worker(session,options);
        assert(worker.initialize(supervisor,"<body><div id='label'>office AV</div></body>",
            "*{margin:0;padding:0;font-weight:400}body{background:#ffffff}"
            "div{display:block;font-size:16px;line-height:18px;color:#224488}") ==
            ScriptTaskWorkerRuntimeInitStatus::Accepted);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_installed_core_text_frame(supervisor,session,options.frame_codec,initial) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        assert(initial.shaped_text_binding() == binding && !initial.frame().shaped_text.empty());
        assert(worker.eval_with_supervisor(supervisor,"document.getElementById('label').textContent='AV';").ok);
        assert(worker.publish_frame(supervisor).accepted());
        assert(take_installed_core_text_frame(supervisor,session,options.frame_codec,changed) ==
            ScriptTaskAppFrameTakeStatus::Accepted);
        assert(initial.frame().shaped_text.front().packet != changed.frame().shaped_text.front().packet);
        worker.stop();
    }
    ScriptTaskFrameRendererOptions render_options;
    shaped->bind_ui_resources(render_options);
    assert(render_options.shaped_text_binding == binding);
    FrameBuffer first(120,60,{255,255,255,255});
    const auto draw = [&](const ScriptTaskAcceptedAppFrame& frame, FrameBuffer& target) {
        return render_installed_core_text_frame(ScriptTaskFrameRenderer(render_options),frame,target,
            {255,255,255,255},nullptr,0,nullptr,nullptr);
    };
    assert(draw(initial,first));
    assert(std::any_of(first.pixels.begin(),first.pixels.end(),[](Color c){return c.r < 100;}));
    FrameBuffer next(120,60,{255,255,255,255});
    assert(draw(changed,next));
    const auto equal = [](const FrameBuffer& a,const FrameBuffer& b) {
        for (std::size_t i=0;i<a.pixels.size();++i) {
            const auto x=a.pixels[i], y=b.pixels[i];
            if (x.r!=y.r || x.g!=y.g || x.b!=y.b || x.a!=y.a) return false;
        }
        return true;
    };
    assert(!equal(first,next));
    auto malformed=changed.frame();
    malformed.shaped_text.back().packet.back()^=1;
    FrameBuffer unchanged=next;
    ScriptTaskFrameRenderStatus status;
    assert(!ScriptTaskFrameRenderer(render_options).render_into(malformed,next,{255,255,255,255},
        nullptr,0,nullptr,&status) && equal(next,unchanged));
    {
        runtime_test_shaped_text::Assets foreign_assets("regular", 7502);
        auto foreign_options = render_options;
        foreign_options.shaped_text_binding = runtime_test_shaped_text::create_binding(foreign_assets);
        // Raw packets must refuse a foreign owner before writing any pixels.
        assert(!ScriptTaskFrameRenderer(foreign_options).render_into(changed.frame(),next,{255,255,255,255},
            nullptr,0,nullptr,&status) && equal(next,unchanged));
        // Accepted envelopes carry their own identity, even with stale UI options.
        assert(render_installed_core_text_frame(ScriptTaskFrameRenderer(foreign_options),changed,next,
            {255,255,255,255},nullptr,0,nullptr,&status) && equal(next,unchanged));
    }
    auto tiny = render_options;
    tiny.max_temporary_pixels = 1;
    assert(!render_installed_core_text_frame(ScriptTaskFrameRenderer(tiny),changed,next,
        {255,255,255,255},nullptr,0,nullptr,&status) && equal(next,unchanged) &&
        status == ScriptTaskFrameRenderStatus::FramebufferRejected);
    const Rect one_pixel{0,0,1,1};
    assert(!render_installed_core_text_frame(ScriptTaskFrameRenderer(tiny),changed,next,
        {255,255,255,255},&one_pixel,1,nullptr,&status) && equal(next,unchanged));
    tiny.max_temporary_pixels = next.pixels.size();
    assert(render_installed_core_text_frame(ScriptTaskFrameRenderer(tiny),changed,next,
        {255,255,255,255},nullptr,0,nullptr,&status) && equal(next,unchanged));
    auto short_target = next;
    short_target.pixels.pop_back();
    const auto short_before = short_target;
    assert(!render_installed_core_text_frame(ScriptTaskFrameRenderer(tiny),changed,short_target,
        {255,255,255,255},nullptr,0,nullptr,&status) && equal(short_target,short_before) &&
        status == ScriptTaskFrameRenderStatus::FramebufferRejected);
    tiny = {};
    const std::weak_ptr<const RuntimeShapedTextBinding> retained_binding = binding;
    supervisor.begin_teardown(session); supervisor.complete_teardown(session);
    shaped.reset(); resources.reset(); original.reset(); bytes.reset(); binding.reset();
    render_options={}; options={};
    // Accepted replay uses its own resource owners after every producer drains.
    assert(!retained_binding.expired());
    FrameBuffer retained(120,60,{4,5,6,255});
    assert(render_installed_core_text_frame(ScriptTaskFrameRenderer{},initial,retained,
        {255,255,255,255},nullptr,0,nullptr,nullptr));
    assert(equal(first,retained));
    initial={}; changed={};
    assert(retained_binding.expired());
    std::cout << "installed contextual v12: owned session, JS mutation, typed paint, atomic refusal and retained replay passed\n";
#else
    pipeline.status=AppInstalledTextPipelineStatus::Ready;
    pipeline.frame_version=12;
    assert(!installed_text_pipeline_supported(pipeline));
    assert(!script_task_app_frame_codec_supports_version(12));
    ScriptTaskSupervisor contextual_supervisor;
    const auto contextual_session = begin_installed_script_session(contextual_supervisor, 44, resources);
    assert(contextual_session.valid());
    ScriptTaskWorkerRuntimeOptions contextual_options;
    contextual_options.frame_codec.version = 12;
    ScriptTaskWorkerRuntime contextual_worker(contextual_session, contextual_options);
    assert(contextual_worker.initialize(contextual_supervisor,
        "<body><div id='row'><div>A</div><div>B</div></div></body>",
        "*{margin:0;padding:0}#row{display:flex;flex-direction:row;flex-wrap:nowrap;width:80px}") !=
        ScriptTaskWorkerRuntimeInitStatus::Accepted);
    assert(!contextual_worker.publish_frame(contextual_supervisor).accepted());
    std::cout << "installed contextual OFF: resources retained; v13 scalar replay passed and v12 refused\n";
#endif
    return 0;
}
