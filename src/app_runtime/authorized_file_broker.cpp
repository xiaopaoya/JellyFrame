#include "app_runtime/authorized_file_broker.h"

#include <limits>

namespace jellyframe {
namespace {

bool path_has_traversal(std::string_view path) {
    std::size_t begin = 1;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string_view part = path.substr(begin, end == std::string_view::npos ? end : end - begin);
        if (part == "." || part == "..") {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return false;
}

AuthorizedFileStatus validate_single_path(std::string_view path, std::size_t max_path_bytes) {
    if (path.empty() || path.size() > max_path_bytes || path.front() != '/' ||
        (path.size() > 1 && path.back() == '/')) {
        return AuthorizedFileStatus::InvalidPath;
    }
    if (path.find('\\') != std::string_view::npos || path.find("://") != std::string_view::npos ||
        path.find("//") != std::string_view::npos) {
        return AuthorizedFileStatus::InvalidPath;
    }
    for (const char ch : path) {
        if (static_cast<unsigned char>(ch) < 0x20U) {
            return AuthorizedFileStatus::InvalidPath;
        }
    }
    if (path_has_traversal(path)) {
        return AuthorizedFileStatus::TraversalRejected;
    }
    return AuthorizedFileStatus::Accepted;
}

} // namespace

const char* authorized_file_operation_name(AuthorizedFileOperation operation) {
    switch (operation) {
    case AuthorizedFileOperation::Read:
        return "read";
    case AuthorizedFileOperation::Write:
        return "write";
    case AuthorizedFileOperation::DeleteEntry:
        return "delete-entry";
    case AuthorizedFileOperation::ListDirectory:
        return "list-directory";
    case AuthorizedFileOperation::RenameEntry:
        return "rename-entry";
    case AuthorizedFileOperation::CreateDirectory:
        return "create-directory";
    }
    return "unknown";
}

const char* authorized_file_status_name(AuthorizedFileStatus status) {
    switch (status) {
    case AuthorizedFileStatus::Accepted:
        return "accepted";
    case AuthorizedFileStatus::CapabilityDenied:
        return "capability-denied";
    case AuthorizedFileStatus::UserApprovalRequired:
        return "user-approval-required";
    case AuthorizedFileStatus::OperationUnsupported:
        return "operation-unsupported";
    case AuthorizedFileStatus::InvalidPath:
        return "invalid-path";
    case AuthorizedFileStatus::TraversalRejected:
        return "traversal-rejected";
    case AuthorizedFileStatus::ByteBudgetExceeded:
        return "byte-budget-exceeded";
    }
    return "unknown";
}

bool authorized_file_path_is_normalized(std::string_view path) {
    return validate_single_path(path, std::numeric_limits<std::size_t>::max()) == AuthorizedFileStatus::Accepted;
}

AuthorizedFileStatus validate_authorized_file_request(const AuthorizedFilePolicy& policy,
                                                      const AuthorizedFileRequest& request) {
    if (!request.user_approved && !request.trusted_system_component) {
        return AuthorizedFileStatus::UserApprovalRequired;
    }

    const AuthorizedFileStatus path_status = validate_single_path(request.path, policy.max_path_bytes);
    if (path_status != AuthorizedFileStatus::Accepted) {
        return path_status;
    }
    if (request.operation == AuthorizedFileOperation::RenameEntry) {
        const AuthorizedFileStatus secondary_status = validate_single_path(request.secondary_path, policy.max_path_bytes);
        if (secondary_status != AuthorizedFileStatus::Accepted) {
            return secondary_status;
        }
    }

    if (request.byte_count > policy.max_transfer_bytes) {
        return AuthorizedFileStatus::ByteBudgetExceeded;
    }

    switch (request.operation) {
    case AuthorizedFileOperation::Read:
        return policy.read || policy.manage ? AuthorizedFileStatus::Accepted : AuthorizedFileStatus::CapabilityDenied;
    case AuthorizedFileOperation::Write:
        return policy.write || policy.manage ? AuthorizedFileStatus::Accepted : AuthorizedFileStatus::CapabilityDenied;
    case AuthorizedFileOperation::DeleteEntry:
    case AuthorizedFileOperation::ListDirectory:
    case AuthorizedFileOperation::RenameEntry:
    case AuthorizedFileOperation::CreateDirectory:
        return policy.manage ? AuthorizedFileStatus::Accepted : AuthorizedFileStatus::CapabilityDenied;
    }
    return AuthorizedFileStatus::OperationUnsupported;
}

} // namespace jellyframe
