#pragma once

#include "app_runtime/app_services.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace jellyframe {

enum class AppXhrReadyState : std::uint8_t {
    Unsent = 0,
    Opened = 1,
    HeadersReceived = 2,
    Loading = 3,
    Done = 4,
};

enum class AppXhrStatus {
    Ok,
    InvalidState,
    InvalidUrl,
    SyncNotSupported,
    UnsupportedMethod,
    SubmitFailed,
};

enum class AppXhrEventKind {
    ReadyStateChange,
    Load,
    Error,
    Timeout,
    Abort,
    LoadEnd,
};

class AppXmlHttpRequest {
public:
    static constexpr std::size_t kMaxQueuedEvents = 8;

    AppXhrStatus open(std::string method, std::string url, bool async = true);
    AppXhrStatus send(AppRuntimeHost& host, NetworkFetchMock& network, std::uint32_t timeout_ms = 0);
    void abort(AppRuntimeHost& host);
    bool handle_completion(AppRuntimeHost& host,
                           NetworkFetchMock& network,
                           const HostServiceCompletion& completion);

    std::size_t take_events(AppXhrEventKind* output, std::size_t capacity);
    void reset();

    AppXhrReadyState ready_state() const {
        return ready_state_;
    }

    std::uint16_t status() const {
        return status_;
    }

    const std::string& response_text() const {
        return response_text_;
    }

    const std::string& response_url() const {
        return response_url_;
    }

    const std::string& content_type() const {
        return content_type_;
    }

    std::uint32_t job_id() const {
        return job_id_;
    }

    bool sent() const {
        return sent_;
    }

private:
    void set_ready_state(AppXhrReadyState state);
    void push_event(AppXhrEventKind kind);
    void finish_error(AppXhrEventKind terminal_event);

    AppXhrReadyState ready_state_ = AppXhrReadyState::Unsent;
    std::uint16_t status_ = 0;
    std::uint32_t job_id_ = 0;
    bool sent_ = false;
    std::string url_;
    std::string response_text_;
    std::string response_url_;
    std::string content_type_;
    std::array<AppXhrEventKind, kMaxQueuedEvents> events_{};
    std::size_t event_count_ = 0;
};

} // namespace jellyframe
