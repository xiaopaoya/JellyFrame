#pragma once

#include <cstddef>
#include <string_view>

namespace jellyframe {

enum class AuthorizedFileOperation {
    Read,
    Write,
    DeleteEntry,
    ListDirectory,
    RenameEntry,
    CreateDirectory,
};

enum class AuthorizedFileStatus {
    Accepted,
    CapabilityDenied,
    UserApprovalRequired,
    OperationUnsupported,
    InvalidPath,
    TraversalRejected,
    ByteBudgetExceeded,
};

struct AuthorizedFilePolicy {
    bool read = false;
    bool write = false;
    bool manage = false;
    std::size_t max_path_bytes = 96;
    std::size_t max_transfer_bytes = 4096;
};

struct AuthorizedFileRequest {
    AuthorizedFileOperation operation = AuthorizedFileOperation::Read;
    std::string_view path;
    std::string_view secondary_path;
    std::size_t byte_count = 0;
    bool user_approved = false;
    bool trusted_system_component = false;
};

const char* authorized_file_operation_name(AuthorizedFileOperation operation);
const char* authorized_file_status_name(AuthorizedFileStatus status);

bool authorized_file_path_is_normalized(std::string_view path);
AuthorizedFileStatus validate_authorized_file_request(const AuthorizedFilePolicy& policy,
                                                      const AuthorizedFileRequest& request);

} // namespace jellyframe
