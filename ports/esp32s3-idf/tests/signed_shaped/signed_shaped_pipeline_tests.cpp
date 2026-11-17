#include "host_store_fixture.h"
#include "allocation_fault.h"
#include "jellyframe_esp32s3_installed_frame_admission.h"
#include "script/script_task_worker_runtime.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/render_tree.h"
#include "render_core/style.h"

#if !JELLYFRAME_RUNTIME_SHAPED_TEXT_API || !JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
#error Qualification requires actual Core shaped DOM producer/paint APIs.
#endif

namespace {
std::vector<std::uint8_t> installed_bytes(AppInstalledBundleBinding& binding, const char* path,
    std::size_t maximum=48*1024) {
    std::vector<std::uint8_t> bytes(maximum);
    std::size_t size = 0;
    CHECK(binding.read_active_resource(path, bytes.data(), bytes.size(), size) == DeviceBundleStatus::Ok);
    CHECK(size > 0);
    bytes.resize(size);
    return bytes;
}
std::string installed_text(AppInstalledBundleBinding& binding, const char* path) {
    const auto bytes=installed_bytes(binding,path);
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
void transact(DeviceImageStore& store, const std::vector<std::uint8_t>& bytes) {
    DeviceInstallTransaction transaction({store.max_bundle_bytes(), 1024});
    CHECK(transaction.begin(77, app_id, static_cast<std::uint32_t>(bytes.size()),
        esp_rom_crc32_le(0, bytes.data(), bytes.size()), false, store).accepted());
    for (std::size_t offset = 0; offset < bytes.size(); offset += 1024)
        CHECK(transaction.append(77, static_cast<std::uint32_t>(offset), bytes.data() + offset,
            std::min<std::size_t>(1024, bytes.size() - offset), store).accepted());
    CHECK(transaction.commit(77, store).accepted());
}
bool equal(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.pixels.size() != b.pixels.size()) return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto x=a.pixels[i],y=b.pixels[i];
        if (x.r!=y.r || x.g!=y.g || x.b!=y.b || x.a!=y.a) return false;
    }
    return true;
}
void image(const std::filesystem::path& path, const FrameBuffer& pixels) {
    std::ofstream stream(path,std::ios::binary);
    CHECK(stream.good());
    stream << "P6\n172 320\n255\n";
    for (const auto c:pixels.pixels) {
        const char rgb[]={static_cast<char>(c.r),static_cast<char>(c.g),static_cast<char>(c.b)};
        stream.write(rgb,3);
    }
}
Rect compare_direct_core(const ScriptTaskAcceptedAppFrame& accepted,
    const RuntimeShapedTextBinding::Handle& binding, std::string_view html,
    std::string_view css, const FrameBuffer& actual, const runtime_shaped::LayoutOptions& options) {
    auto dom=HtmlParser{}.parse(std::string(html));
    StyleResolver styles(CssParser{}.parse(std::string(css)));
    auto tree=RenderTreeBuilder(styles).build(*dom);
    LayoutEngine engine(styles);
    runtime_shaped::LayoutResult layout;
    auto work=binding->work_limit();
    CHECK(binding->layout_document(engine,*tree,172,320,options,work,layout)==runtime_shaped::Status::ok);
    runtime_shaped::LayerCapture capture;
    CHECK(LayerTreeBuilder{}.build_shaped(layout,capture));
    std::size_t at=0;
    for (std::size_t p=0;p<layout.paragraphs().size();++p) {
        const auto& paragraph=layout.paragraphs()[p];
        CHECK(paragraph.align==TextAlign::Start);
        for (const auto& line:paragraph.paragraph.lines()) {
            CHECK(at<accepted.frame().shaped_text.size());
            const auto& transported=accepted.frame().shaped_text[at++];
            CHECK(transported.packet==line.packet);
            CHECK(transported.after_command_count==capture.after_command_counts[p]);
            CHECK(transported.baseline_x_q1000==paragraph.origin_x_q1000);
            CHECK(transported.baseline_y_q1000==paragraph.origin_y_q1000+line.baseline_y_q1000);
            CHECK(transported.clip.x==0 && transported.clip.y==0 &&
                transported.clip.width==172 && transported.clip.height==320);
            CHECK(transported.color.r==paragraph.color.r && transported.color.g==paragraph.color.g &&
                transported.color.b==paragraph.color.b && transported.color.a==paragraph.color.a);
        }
    }
    CHECK(at==accepted.frame().shaped_text.size());
    FrameBuffer reference(172,320,{255,255,255,255});
    SoftwareRasterizer rasterizer;
    work=binding->work_limit();
    at=0;
    for (std::size_t cmd=0;cmd<=capture.layer_tree->display_list.size();++cmd) {
        while (at<accepted.frame().shaped_text.size() && accepted.frame().shaped_text[at].after_command_count==cmd) {
            const auto& line=accepted.frame().shaped_text[at++];
            CHECK(runtime_shaped::paint(binding->core_binding(),line.packet,reference,
                line.baseline_x_q1000,line.baseline_y_q1000,line.clip,line.color,{},work)==runtime_shaped::Status::ok);
        }
        if (cmd<capture.layer_tree->display_list.size())
            rasterizer.rasterize(capture.layer_tree->display_list[cmd],reference,{0,0,172,320});
    }
    CHECK(equal(reference,actual));
    std::vector<const LayoutBox*> pending{layout.tree()};
    while (!pending.empty()) {
        const auto* box=pending.back();pending.pop_back();
        if (box->node && box->node->attribute("id")=="button") return box->rect;
        for (const auto& child:box->children) pending.push_back(child.get());
    }
    CHECK(false);
    return {};
}
}

int main(int argc,char** argv) {
    CHECK(argc==3);
    const std::string mode=argv[1];
    const std::filesystem::path fixture=argv[2];
    auto bytes=read(fixture/"full-shaped.jfsg");
    const auto package_bytes=bytes.size();
    auto store=boot();
    CHECK(bytes.size()>1044480 && bytes.size()<2093056);
    CHECK(store->max_bundle_bytes()==2093056);
    if (mode=="capacity") {
        partition.size=3*1024*1024;
        CHECK(store->max_bundle_bytes()==1044480);
        const auto unchanged=flash;
        DeviceInstallTransaction transaction({store->max_bundle_bytes(),1024});
        CHECK(transaction.begin(1,app_id,static_cast<std::uint32_t>(bytes.size()),
            esp_rom_crc32_le(0,bytes.data(),bytes.size()),false,*store).status==DeviceInstallStatus::InvalidRequest);
        CHECK(flash==unchanged);
        std::printf("full-shaped capacity refusal checks=%u bytes=%zu\n",checks,bytes.size());
        return 0;
    }
    const auto crypto_before=crypto_calls;
    transact(*store,bytes);
    CHECK(crypto_calls>crypto_before);
    if (mode=="reboot") {
        const auto calls=crypto_calls;
        store=boot();
        CHECK(crypto_calls>calls);
        assert_version(*store,1);
    }
    if (mode=="tamper") {
        auto bad=bytes;
        const auto manifest=std::uint32_t(bad[16])|(std::uint32_t(bad[17])<<8)|
            (std::uint32_t(bad[18])<<16)|(std::uint32_t(bad[19])<<24);
        bad[64+manifest]^=1;
        const auto request=stage(*store,bad);
        CHECK(!store->verify_staging(request));
        CHECK(!store->commit_staging(request));
        store->abort_staging(request.transaction_id);
        assert_version(*store,1);
    }
    AppRuntimeHost host({4,2,4,4096,1});
    auto binding=std::make_unique<AppInstalledBundleBinding>(*store);
    CHECK(binding->launch(host,app_id).launched());
    DeviceBundleInspectionWorkspace workspace;
    auto selected=binding->select_active_text_pipeline(workspace,2*1024*1024);
    std::printf("selected pipeline status=%u\n",unsigned(selected.status));
    CHECK(selected.status==AppInstalledTextPipelineStatus::Ready);
    CHECK(selected.frame_version==11 && selected.binding && selected.binding->valid());
    CHECK(installed_text_pipeline_supported(selected));
    if (mode=="binding_oom") {
        allocation_fault::begin();
        auto measured=binding->select_active_text_pipeline(workspace,2*1024*1024);
        const auto allocations=allocation_fault::end();
        CHECK(measured.status==AppInstalledTextPipelineStatus::Ready);
        CHECK(allocations>5 && allocations<4096);
        measured.binding.reset();
        for (std::size_t n=0;n<allocations;++n) {
            AppInstalledTextPipeline refused;
            allocation_fault::begin(n);
            try { refused=binding->select_active_text_pipeline(workspace,2*1024*1024); }
            catch (...) { allocation_fault::end();std::fprintf(stderr,"binding allocation escaped at %zu\n",n);CHECK(false); }
            allocation_fault::end();
            CHECK(allocation_fault::triggered());
            CHECK(refused.status!=AppInstalledTextPipelineStatus::Ready && !refused.binding);
            CHECK(selected.binding->valid());
            auto retry=binding->select_active_text_pipeline(workspace,2*1024*1024);
            CHECK(retry.status==AppInstalledTextPipelineStatus::Ready && retry.binding->valid());
        }
        std::printf("binding OOM sweep allocations=%zu; every refusal recovered\n",allocations);
    }
    if (mode=="resource_budget") {
        const auto before=selected.binding;
        const auto refused=binding->select_active_text_pipeline(workspace,1044480);
        CHECK(refused.status!=AppInstalledTextPipelineStatus::Ready);
        CHECK(before->valid() && before==selected.binding);
    }
    const auto html=installed_text(*binding,"/index.html");
    const auto css=installed_text(*binding,"/app.css");
    const auto js=installed_text(*binding,"/app.js");
    auto fonts=std::make_shared<const std::vector<std::uint8_t>>(
        installed_bytes(*binding,"/font.jffont2",2*1024*1024));
    CHECK(fonts->size()==716274);
    CHECK(*fonts==read(fixture/"font.jffont2"));
    CoreFontResourceView system{fonts->data(),fonts->size()};
    InstalledResourceSnapshot snapshot;
    snapshot.entries.push_back({"/app.css",HostResourceKind::Stylesheet,{css.begin(),css.end()}});
    snapshot.entries.push_back({"/app.js",HostResourceKind::ClassicScript,{js.begin(),js.end()}});
    InstalledAppResources::Handle scalar;
    CHECK(create_installed_app_resources(std::move(snapshot),{},"/index.html",&system,1,fonts,
        esp_timer_get_time,scalar));
    auto mutable_resources=std::make_shared<InstalledAppResources>(*scalar);
    mutable_resources->shaped_text_binding=selected.binding;
    mutable_resources->image_provider.reset();
    InstalledAppResources::Handle resources=mutable_resources;
    mutable_resources.reset();scalar.reset();
    ScriptTaskSupervisorOptions supervisor_options;
    supervisor_options.worker_inbox={8,256};
    supervisor_options.frame_mailbox={4,4096};
    supervisor_options.frame_leases={4,65536,262144};
    supervisor_options.fatal_mailbox={4,40};
    ScriptTaskSupervisor supervisor(supervisor_options);
    const auto session=begin_installed_script_session(supervisor,host.current().id,resources);
    CHECK(session.valid());
    ScriptTaskWorkerRuntimeOptions options;
    resources->bind_worker_resources(options);
    options.viewport={0,0,172,320};
    options.frame_codec={256,32768,32,65536};
    options.frame_codec.version=11;options.frame_codec.max_groups=64;
    options.input_codec={32,256};
    options.shaped_layout.font_family_hash=selected.font_family_hash;
    if (mode=="worker_init_oom") {
        auto measured=std::make_unique<ScriptTaskWorkerRuntime>(session,options);
        allocation_fault::begin();
        const auto status=measured->initialize(supervisor,html,css);
        const auto allocations=allocation_fault::end();
        CHECK(status==ScriptTaskWorkerRuntimeInitStatus::Accepted);
        CHECK(allocations>5 && allocations<10000);
        measured.reset();
        std::size_t refused=0,injected=0;
        for (std::size_t n=0;n<allocations;++n) {
            auto candidate=std::make_unique<ScriptTaskWorkerRuntime>(session,options);
            allocation_fault::begin(n);
            ScriptTaskWorkerRuntimeInitStatus outcome;
            try { outcome=candidate->initialize(supervisor,html,css); }
            catch (...) { allocation_fault::end();std::fprintf(stderr,"worker init allocation escaped at %zu\n",n);CHECK(false); }
            allocation_fault::end();
            if (!allocation_fault::triggered()) {
                // First initialization may populate process-wide caches. Later
                // runs need not reach every index from the cold allocation count.
                CHECK(outcome==ScriptTaskWorkerRuntimeInitStatus::Accepted);
                continue;
            }
            ++injected;
            if (outcome!=ScriptTaskWorkerRuntimeInitStatus::Accepted) {
                ++refused;
                CHECK(!candidate->initialized() && candidate->fatal());
            } else CHECK(candidate->initialized() && !candidate->fatal());
            candidate.reset();
            CHECK(selected.binding->valid());
        }
        CHECK(refused>0);
        std::printf("worker init OOM sweep cold_allocations=%zu injected=%zu refused=%zu; fresh worker recovery follows\n",allocations,injected,refused);
    }
    if (mode=="worker_mutation_oom" || mode=="worker_input_oom") {
        const bool use_input=mode=="worker_input_oom";
        const auto drain_packets=[&] {
            ScriptTaskPacket pending;
            while (supervisor.take_worker_packet(session,pending)) {}
            ScriptTaskAcceptedAppFrame discarded;
            while (take_installed_core_text_frame(supervisor,session,options.frame_codec,discarded)==ScriptTaskAppFrameTakeStatus::Accepted) {}
        };
        const char* mutation="document.getElementById('label').textContent='AV office fi 中文';document.getElementById('label').style.width='104px';";
        auto make_worker=[&] {
            auto candidate=std::make_unique<ScriptTaskWorkerRuntime>(session,options);
            CHECK(candidate->initialize(supervisor,html,css)==ScriptTaskWorkerRuntimeInitStatus::Accepted);
            if (use_input) {
                CHECK(candidate->eval_with_supervisor(supervisor,js).ok);
                // title26 + margin6 + one label line22 + margin8 = button y62.
                CHECK(post_script_task_input(supervisor,session,1,
                    {ScriptTaskInputKind::PointerDown,2,64,0,0,0,1,0,0,{}},options.input_codec).accepted());
                CHECK(candidate->process_one(supervisor).input_accepted);
                CHECK(post_script_task_input(supervisor,session,2,
                    {ScriptTaskInputKind::PointerUp,2,64,0,0,0,0,0,0,{}},options.input_codec).accepted());
            }
            return candidate;
        };
        const auto run_mutation=[&](ScriptTaskWorkerRuntime& candidate) {
            if (use_input) {
                const auto step=candidate.process_one(supervisor);
                return step.input_accepted && !step.fatal && step.frame_published;
            }
            return candidate.eval_with_supervisor(supervisor,mutation).ok;
        };
        auto measured=make_worker();
        CHECK(measured->publish_frame(supervisor).accepted());
        ScriptTaskAcceptedAppFrame retained;
        CHECK(take_installed_core_text_frame(supervisor,session,options.frame_codec,retained)==ScriptTaskAppFrameTakeStatus::Accepted);
        ScriptTaskFrameRenderer retained_renderer;
        const auto render_retained=[&] {
            FrameBuffer pixels(172,320,{255,255,255,255});
            ScriptTaskFrameRenderStatus status;
            CHECK(render_installed_core_text_frame(retained_renderer,retained,pixels,
                {255,255,255,255},nullptr,0,nullptr,&status));
            CHECK(status==ScriptTaskFrameRenderStatus::Accepted);
            return pixels;
        };
        const auto retained_pixels=render_retained();
        allocation_fault::begin();
        const auto success=run_mutation(*measured);
        const auto allocations=allocation_fault::end();
        CHECK(success && !measured->fatal());
        CHECK(allocations>5 && allocations<10000);
        measured.reset();
        drain_packets();
        std::size_t injected=0,refused=0,retryable=0;
        for (std::size_t n=0;n<allocations;++n) {
            auto candidate=make_worker();
            allocation_fault::begin(n);
            bool outcome=false;
            try { outcome=run_mutation(*candidate); }
            catch (...) { allocation_fault::end();std::fprintf(stderr,"worker mutation allocation escaped at %zu\n",n);CHECK(false); }
            allocation_fault::end();
            if (allocation_fault::triggered()) ++injected;
            if (!outcome || candidate->fatal()) {
                ++refused;
                if (!candidate->fatal()) {
                    CHECK(use_input);
                    // Codec/lease allocation refusals may leave the worker
                    // live and its pending frame retryable instead of fatal.
                    CHECK(candidate->publish_frame(supervisor).accepted());
                    ++retryable;
                }
            }
            candidate.reset();
            drain_packets();
            CHECK(selected.binding->valid());
            CHECK(equal(render_retained(),retained_pixels));
        }
        CHECK(refused>0 && injected>0);
        std::printf("worker %s OOM sweep cold_allocations=%zu injected=%zu refused=%zu retryable=%zu; fresh worker recovery follows\n",use_input?"input":"mutation",allocations,injected,refused,retryable);
    }
    if (mode=="worker_fatal_oom") {
        ScriptTaskWorkerRuntime candidate(session,options);
        CHECK(candidate.initialize(supervisor,html,css)==ScriptTaskWorkerRuntimeInitStatus::Accepted);
        allocation_fault::begin(0);
        const auto refused=candidate.eval_with_supervisor(supervisor,"1+1");
        allocation_fault::end();
        CHECK(allocation_fault::triggered() && !refused.ok && candidate.fatal());
        allocation_fault::begin(0);
        CHECK(!candidate.publish_fatal(supervisor));
        allocation_fault::end();
        CHECK(allocation_fault::triggered());
        CHECK(candidate.publish_fatal(supervisor));
        ScriptTaskPacket fatal;
        CHECK(supervisor.take_fatal(fatal));
        CHECK(fatal.session==session && fatal.payload.size()==40);
        CHECK(candidate.publish_fatal(supervisor));
        CHECK(!supervisor.take_fatal(fatal));
        candidate.stop();
        std::printf("fatal record allocation refusal/retry publishes exactly once\n");
    }
    auto worker=std::make_unique<ScriptTaskWorkerRuntime>(session,options);
    const auto init=worker->initialize(supervisor,html,css);
    std::printf("worker init status=%u\n",unsigned(init));
    CHECK(init==ScriptTaskWorkerRuntimeInitStatus::Accepted);
    CHECK(worker->eval_with_supervisor(supervisor,js,"/app.js").ok);
    ScriptTaskAcceptedAppFrame initial,changed;
    const auto take=[&](ScriptTaskAcceptedAppFrame& accepted) {
        CHECK(take_installed_core_text_frame(supervisor,session,options.frame_codec,accepted)==ScriptTaskAppFrameTakeStatus::Accepted);
        CHECK(accepted.shaped_text_binding()==selected.binding && !accepted.frame().shaped_text.empty());
        for (const auto& command:accepted.frame().display_list)
            CHECK(command.type()!=DisplayCommandType::PreparedText && command.type()!=DisplayCommandType::Text);
    };
    ScriptTaskFrameRenderer renderer;
    const auto render=[&](const ScriptTaskAcceptedAppFrame& accepted) {
        FrameBuffer pixels(172,320,{255,255,255,255});
        ScriptTaskFrameRenderStatus status;
        CHECK(render_installed_core_text_frame(renderer,accepted,pixels,{255,255,255,255},nullptr,0,nullptr,&status));
        CHECK(status==ScriptTaskFrameRenderStatus::Accepted);
        CHECK(std::any_of(pixels.pixels.begin(),pixels.pixels.end(),[](Color c){return c.r<100 && c.g<100;}));
        return pixels;
    };
    CHECK(worker->publish_frame(supervisor).accepted());
    take(initial);
    const auto original=render(initial);
    auto button=compare_direct_core(initial,selected.binding,html,css,original,options.shaped_layout);
    if (mode=="pipeline") image(fixture/"initial.ppm",original);
    std::uint32_t seq=0;
    changed=initial;
    for (unsigned round=0;round<3;++round) {
        // DOM input uses the worker's ordinary hit tree, not optional external
        // target-key records. Use independently computed current Core geometry.
        CHECK(button.width>0 && button.height>0);
        for (auto kind:{ScriptTaskInputKind::PointerDown,ScriptTaskInputKind::PointerUp}) {
            CHECK(post_script_task_input(supervisor,session,++seq,
                {kind,button.x+2,button.y+2,0,0,0,kind==ScriptTaskInputKind::PointerDown?1:0,0,0,{}},options.input_codec).accepted());
            const auto step=worker->process_one(supervisor);
            CHECK(step.input_accepted && !step.fatal);
            if (kind==ScriptTaskInputKind::PointerUp) CHECK(step.frame_published);
        }
        take(changed);
        const auto pixels=render(changed);
        CHECK(!equal(pixels,original));
        auto expected=html;
        const std::string label="<div id=\"label\">";
        const auto start=expected.find(label);
        CHECK(start!=std::string::npos);
        const auto end=expected.find("</div>",start);
        CHECK(end!=std::string::npos);
        expected.replace(start,end-start,"<div id=\"label\" style=\"width:"+
            std::string(round%2==0?"104px":"148px")+"\">"+
            (round%2==0?"AV office fi 中文":"office AV fi 中 e\u0301"));
        const auto next=expected.find("NEXT 0");
        CHECK(next!=std::string::npos);
        expected.replace(next,6,"NEXT "+std::to_string(round+1));
        button=compare_direct_core(changed,selected.binding,expected,css,pixels,options.shaped_layout);
        if (mode=="pipeline" && round==0) image(fixture/"clicked.ppm",pixels);
    }
    worker->stop();
    worker.reset();
    CHECK(supervisor.begin_teardown(session).session==session);
    CHECK(supervisor.complete_teardown(session).session==session);
    binding->terminate_current(host,AppTeardownReason::NormalExit);
    binding.reset();store.reset();
    resources.reset();fonts.reset();selected.binding.reset();options.shaped_text_binding.reset();
    options.text_catalog.reset();options.image_provider.reset();changed={};
    std::fill(flash.begin(),flash.end(),0xff);
    bytes.clear();
    CHECK(equal(render(initial),original));
    std::printf("full-shaped %s PASS bytes=%zu checks=%u crypto_calls=%u records=%zu\n",mode.c_str(),package_bytes,checks,crypto_calls,initial.frame().shaped_text.size());
}
namespace jellyframe_esp32s3 {
const ResourceBundle& generated_resource_bundle() { static const ResourceBundle empty{};return empty; }
}
