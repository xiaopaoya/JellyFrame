#include "app_runtime/authorized_file_broker.h"

#include <cassert>
#include <string>

using namespace jellyframe;

namespace {

void normalized_paths_are_strict() {
    assert(authorized_file_path_is_normalized("/documents/weather.json"));
    assert(!authorized_file_path_is_normalized(""));
    assert(!authorized_file_path_is_normalized("documents/weather.json"));
    assert(!authorized_file_path_is_normalized("/documents/../secret.txt"));
    assert(!authorized_file_path_is_normalized("/documents//secret.txt"));
    assert(!authorized_file_path_is_normalized("/documents\\secret.txt"));
    assert(!authorized_file_path_is_normalized("/documents/file.txt/"));
    assert(!authorized_file_path_is_normalized("file://documents/file.txt"));
}

void policy_requires_capability_and_user_approval() {
    const AuthorizedFilePolicy policy{true, false, false, 64, 32};
    AuthorizedFileRequest request;
    request.operation = AuthorizedFileOperation::Read;
    request.path = "/documents/weather.json";
    request.byte_count = 16;

    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::UserApprovalRequired);
    request.user_approved = true;
    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::Accepted);

    request.operation = AuthorizedFileOperation::Write;
    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::CapabilityDenied);
}

void manage_policy_covers_file_manager_operations() {
    const AuthorizedFilePolicy policy{false, false, true, 64, 128};
    AuthorizedFileRequest request;
    request.operation = AuthorizedFileOperation::RenameEntry;
    request.path = "/documents/a.txt";
    request.secondary_path = "/documents/b.txt";
    request.user_approved = true;

    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::Accepted);
    request.secondary_path = "/documents/../b.txt";
    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::TraversalRejected);
}

void budgets_and_names_are_stable() {
    const AuthorizedFilePolicy policy{true, true, false, 12, 8};
    AuthorizedFileRequest request;
    request.operation = AuthorizedFileOperation::Write;
    request.path = "/a.txt";
    request.byte_count = 9;
    request.trusted_system_component = true;

    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::ByteBudgetExceeded);
    request.byte_count = 8;
    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::Accepted);
    request.path = "/too-long-name.txt";
    assert(validate_authorized_file_request(policy, request) == AuthorizedFileStatus::InvalidPath);

    assert(std::string(authorized_file_operation_name(AuthorizedFileOperation::CreateDirectory)) == "create-directory");
    assert(std::string(authorized_file_status_name(AuthorizedFileStatus::CapabilityDenied)) == "capability-denied");
}

} // namespace

int main() {
    normalized_paths_are_strict();
    policy_requires_capability_and_user_approval();
    manage_policy_covers_file_manager_operations();
    budgets_and_names_are_stable();
    return 0;
}
