#pragma once

#include <iostream>

// ABI9 flat scalar resources are not a substitute for the contextual v12 route.
inline int run_installed_contextual_text_tests() {
    using namespace jellyframe;
    using namespace jellyframe_esp32s3;
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(runtime_test_fonts::font_bytes());
    CoreFontResourceView system{bytes->data(), bytes->size()};
    InstalledAppResources::Handle resources;
    assert(create_installed_app_resources({}, {}, "/index.html", &system, 1, bytes, nullptr, resources));
    assert(resources && resources->text_catalog && !resources->image_provider);
    InstalledResourceSnapshot images;
    images.entries.push_back({"/bad.bmp", HostResourceKind::Image, {1,2,3}});
    auto original = resources;
    assert(!create_installed_app_resources(std::move(images), {}, "/index.html", &system, 1, bytes, nullptr, resources));
    assert(resources == original);
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.frame_mailbox = {4,4096};
    supervisor_options.frame_leases = {4,32768,65536};
    ScriptTaskSupervisor supervisor(supervisor_options);
    assert(!script_task_app_frame_codec_supports_version(10));
    assert(!begin_installed_script_session(supervisor,41,resources).valid());
    AppInstalledTextPipeline pipeline;
    pipeline.status = AppInstalledTextPipelineStatus::Scalar;
    assert(!installed_text_pipeline_supported(pipeline));
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
    runtime_test_shaped_text::Assets assets("regular", 7501);
    auto binding = runtime_test_shaped_text::create_binding(assets);
    auto shaped = std::make_shared<InstalledAppResources>(*resources);
    shaped->shaped_text_binding = binding;
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
    std::cout << "installed contextual OFF: resources retained; v12 and scalar downgrade refused\n";
#endif
    return 0;
}
