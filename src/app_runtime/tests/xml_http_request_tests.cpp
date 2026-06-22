#include "app_runtime/xml_http_request.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "xml_http_request check failed: " << message << '\n';
    std::abort();
}

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{4, 4, 8, 4096, 1});
}

std::vector<HostServiceCompletion> pump(AppRuntimeHost& host) {
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    return accepted;
}

std::vector<AppXhrEventKind> take_events(AppXmlHttpRequest& xhr) {
    AppXhrEventKind events[AppXmlHttpRequest::kMaxQueuedEvents];
    const std::size_t count = xhr.take_events(events, AppXmlHttpRequest::kMaxQueuedEvents);
    return std::vector<AppXhrEventKind>(events, events + count);
}

void xhr_get_maps_network_completion_to_standard_state() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.weather", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    check(network.add_fixture(NetworkFetchFixture{
        "https://api.example/weather",
        200,
        "application/json",
        "{\"temp\":21}",
    }), "fixture accepted");

    AppXmlHttpRequest xhr;
    check(xhr.open("GET", "https://api.example/weather", true) == AppXhrStatus::Ok, "xhr open");
    check(xhr.ready_state() == AppXhrReadyState::Opened, "xhr opened state");
    std::vector<AppXhrEventKind> events = take_events(xhr);
    check(events.size() == 1 && events[0] == AppXhrEventKind::ReadyStateChange, "xhr open event");

    check(xhr.send(host, network, 1000) == AppXhrStatus::Ok, "xhr send");
    check(xhr.sent(), "xhr sent flag");
    check(network.complete_next(host), "network complete");
    const std::vector<HostServiceCompletion> completions = pump(host);
    check(completions.size() == 1, "xhr completion pumped");
    check(xhr.handle_completion(host, network, completions.front()), "xhr handled completion");

    check(xhr.ready_state() == AppXhrReadyState::Done, "xhr done state");
    check(xhr.status() == 200, "xhr status");
    check(xhr.content_type() == "application/json", "xhr content type");
    check(xhr.response_text() == "{\"temp\":21}", "xhr response text");
    check(!xhr.sent(), "xhr sent cleared");
    check(host.handles().active_count() == 0, "xhr response handle released");

    events = take_events(xhr);
    check(events.size() == 5, "xhr completion event count");
    check(events[0] == AppXhrEventKind::ReadyStateChange, "headers event");
    check(events[1] == AppXhrEventKind::ReadyStateChange, "loading event");
    check(events[2] == AppXhrEventKind::ReadyStateChange, "done event");
    check(events[3] == AppXhrEventKind::Load, "load event");
    check(events[4] == AppXhrEventKind::LoadEnd, "loadend event");
}

void xhr_rejects_non_subset_calls() {
    AppXmlHttpRequest xhr;
    check(xhr.open("POST", "https://api.example", true) == AppXhrStatus::UnsupportedMethod,
          "xhr rejects POST");
    check(xhr.open("GET", "https://api.example", false) == AppXhrStatus::SyncNotSupported,
          "xhr rejects sync");
    check(xhr.open("GET", "", true) == AppXhrStatus::InvalidUrl, "xhr rejects empty url");
}

void xhr_send_failure_becomes_error_event() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.denied", AppRole::App);
    NetworkFetchMock network;
    AppXmlHttpRequest xhr;
    check(xhr.open("GET", "https://api.example/denied", true) == AppXhrStatus::Ok, "xhr denied open");
    take_events(xhr);
    check(xhr.send(host, network) == AppXhrStatus::SubmitFailed, "xhr denied submit");
    check(xhr.ready_state() == AppXhrReadyState::Done, "xhr denied done");
    check(xhr.status() == 0, "xhr denied status 0");
    std::vector<AppXhrEventKind> events = take_events(xhr);
    check(events.size() == 3, "xhr denied event count");
    check(events[0] == AppXhrEventKind::ReadyStateChange, "xhr denied readystatechange");
    check(events[1] == AppXhrEventKind::Error, "xhr denied error");
    check(events[2] == AppXhrEventKind::LoadEnd, "xhr denied loadend");
}

void xhr_abort_cancels_pending_request() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.abort", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});
    check(network.add_fixture(NetworkFetchFixture{"/data/slow.txt", 200, "text/plain", "ok"}), "abort fixture");

    AppXmlHttpRequest xhr;
    check(xhr.open("GET", "/data/slow.txt", true) == AppXhrStatus::Ok, "xhr abort open");
    take_events(xhr);
    check(xhr.send(host, network) == AppXhrStatus::Ok, "xhr abort send");
    check(host.requests().size() == 1, "xhr request queued");
    xhr.abort(host);
    check(host.requests().empty(), "xhr abort cancels queued request");
    check(xhr.ready_state() == AppXhrReadyState::Unsent, "xhr abort unsent");
    check(xhr.status() == 0, "xhr abort status");

    std::vector<AppXhrEventKind> events = take_events(xhr);
    check(events.size() == 3, "xhr abort event count");
    check(events[0] == AppXhrEventKind::ReadyStateChange, "xhr abort readystatechange");
    check(events[1] == AppXhrEventKind::Abort, "xhr abort event");
    check(events[2] == AppXhrEventKind::LoadEnd, "xhr abort loadend");
}

void xhr_missing_response_record_releases_handle() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.missing-record", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 128, 256});

    AppXmlHttpRequest xhr;
    check(xhr.open("GET", "/data/missing-record.json", true) == AppXhrStatus::Ok, "xhr missing-record open");
    take_events(xhr);
    check(xhr.send(host, network) == AppXhrStatus::Ok, "xhr missing-record send");
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, host.current_app_instance_id(), 8);
    check(handle != 0, "xhr missing-record handle allocated");
    HostServiceCompletion completion{
        xhr.job_id(),
        HostServiceJobKind::NetworkFetch,
        HostServiceStatus::Completed,
        host.current_app_instance_id(),
        handle,
        0,
        8,
    };

    check(xhr.handle_completion(host, network, completion), "xhr missing-record completion handled");
    check(host.handles().active_count() == 0, "xhr missing-record handle released");
    const std::vector<AppXhrEventKind> events = take_events(xhr);
    check(events.size() == 3, "xhr missing-record event count");
    check(events[0] == AppXhrEventKind::ReadyStateChange, "xhr missing-record readystatechange");
    check(events[1] == AppXhrEventKind::Error, "xhr missing-record error");
    check(events[2] == AppXhrEventKind::LoadEnd, "xhr missing-record loadend");
}

} // namespace

int main() {
    xhr_get_maps_network_completion_to_standard_state();
    xhr_rejects_non_subset_calls();
    xhr_send_failure_becomes_error_event();
    xhr_abort_cancels_pending_request();
    xhr_missing_response_record_releases_handle();
    return 0;
}
