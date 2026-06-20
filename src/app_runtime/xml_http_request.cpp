#include "app_runtime/xml_http_request.h"

#include <algorithm>

namespace jellyframe {
namespace {

std::string ascii_uppercase(std::string value) {
    for (char& ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return value;
}

bool local_or_absolute_url_is_valid(const std::string& url) {
    return !url.empty() && url.find('\0') == std::string::npos;
}

} // namespace

void AppXmlHttpRequest::push_event(AppXhrEventKind kind) {
    if (event_count_ >= events_.size()) {
        return;
    }
    events_[event_count_++] = kind;
}

void AppXmlHttpRequest::set_ready_state(AppXhrReadyState state) {
    if (ready_state_ == state) {
        return;
    }
    ready_state_ = state;
    push_event(AppXhrEventKind::ReadyStateChange);
}

AppXhrStatus AppXmlHttpRequest::open(std::string method, std::string url, bool async) {
    if (!async) {
        return AppXhrStatus::SyncNotSupported;
    }
    method = ascii_uppercase(std::move(method));
    if (method != "GET") {
        return AppXhrStatus::UnsupportedMethod;
    }
    if (!local_or_absolute_url_is_valid(url)) {
        return AppXhrStatus::InvalidUrl;
    }

    reset();
    method_ = std::move(method);
    url_ = std::move(url);
    response_url_ = url_;
    set_ready_state(AppXhrReadyState::Opened);
    return AppXhrStatus::Ok;
}

AppXhrStatus AppXmlHttpRequest::send(AppRuntimeHost& host,
                                     NetworkFetchMock& network,
                                     std::uint32_t timeout_ms) {
    if (ready_state_ != AppXhrReadyState::Opened || sent_) {
        return AppXhrStatus::InvalidState;
    }
    const AppServiceSubmitResult submitted = network.submit_fetch(host, url_, timeout_ms);
    if (!submitted.accepted()) {
        finish_error(submitted.rejected_status == HostServiceStatus::Timeout
            ? AppXhrEventKind::Timeout
            : AppXhrEventKind::Error);
        return AppXhrStatus::SubmitFailed;
    }
    job_id_ = submitted.job_id;
    sent_ = true;
    return AppXhrStatus::Ok;
}

void AppXmlHttpRequest::abort(AppRuntimeHost& host) {
    if (job_id_ != 0) {
        host.requests().cancel_pending(job_id_);
    }
    job_id_ = 0;
    sent_ = false;
    status_ = 0;
    response_text_.clear();
    content_type_.clear();
    set_ready_state(AppXhrReadyState::Unsent);
    push_event(AppXhrEventKind::Abort);
    push_event(AppXhrEventKind::LoadEnd);
}

void AppXmlHttpRequest::finish_error(AppXhrEventKind terminal_event) {
    job_id_ = 0;
    sent_ = false;
    status_ = 0;
    response_text_.clear();
    content_type_.clear();
    set_ready_state(AppXhrReadyState::Done);
    push_event(terminal_event);
    push_event(AppXhrEventKind::LoadEnd);
}

bool AppXmlHttpRequest::handle_completion(AppRuntimeHost& host,
                                          NetworkFetchMock& network,
                                          const HostServiceCompletion& completion) {
    if (completion.kind != HostServiceJobKind::NetworkFetch || completion.job_id != job_id_) {
        return false;
    }

    if (completion.status != HostServiceStatus::Completed || completion.handle == 0) {
        finish_error(completion.status == HostServiceStatus::Timeout
            ? AppXhrEventKind::Timeout
            : completion.status == HostServiceStatus::Cancelled
                ? AppXhrEventKind::Abort
                : AppXhrEventKind::Error);
        return true;
    }

    const NetworkFetchRecord* record = network.response(completion.handle);
    if (record == nullptr) {
        finish_error(AppXhrEventKind::Error);
        return true;
    }

    status_ = record->status_code;
    content_type_ = record->content_type;
    response_text_ = record->body;
    set_ready_state(AppXhrReadyState::HeadersReceived);
    set_ready_state(AppXhrReadyState::Loading);
    set_ready_state(AppXhrReadyState::Done);
    job_id_ = 0;
    sent_ = false;
    push_event(AppXhrEventKind::Load);
    push_event(AppXhrEventKind::LoadEnd);
    network.release_response(host, completion.handle);
    return true;
}

std::size_t AppXmlHttpRequest::take_events(AppXhrEventKind* output, std::size_t capacity) {
    const std::size_t count = std::min(capacity, event_count_);
    for (std::size_t index = 0; index < count; ++index) {
        output[index] = events_[index];
    }
    if (count < event_count_) {
        std::copy(events_.begin() + static_cast<std::ptrdiff_t>(count),
                  events_.begin() + static_cast<std::ptrdiff_t>(event_count_),
                  events_.begin());
    }
    event_count_ -= count;
    return count;
}

void AppXmlHttpRequest::reset() {
    ready_state_ = AppXhrReadyState::Unsent;
    status_ = 0;
    job_id_ = 0;
    sent_ = false;
    method_.clear();
    url_.clear();
    response_text_.clear();
    response_url_.clear();
    content_type_.clear();
    event_count_ = 0;
}

} // namespace jellyframe
