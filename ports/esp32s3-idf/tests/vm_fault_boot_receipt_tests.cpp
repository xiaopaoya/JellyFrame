#include "jellyframe_esp32s3_vm_fault_receipt.h"

#include <cassert>
#include <string>

using namespace jellyframe_esp32s3;

int main() {
    constexpr std::string_view app_id = "org.jellyframe.vm-boundary";
    const auto recorded = make_vm_fault_boot_receipt(app_id, 192, 10);
    VmFaultBootReceipt result{};

    auto retained = recorded;
    assert(consume_vm_fault_boot_receipt(retained, true, result));
    assert(std::string_view(result.app_id.data()) == app_id);
    assert(result.generation == 192 && result.code == 10);
    // A second unrelated software reset must not replay the diagnostic.
    assert(!consume_vm_fault_boot_receipt(retained, true, result));

    retained = recorded;
    assert(!consume_vm_fault_boot_receipt(retained, false, result));
    assert(!consume_vm_fault_boot_receipt(retained, true, result));

    retained = recorded;
    retained.app_id[5] ^= 1;
    assert(!consume_vm_fault_boot_receipt(retained, true, result));
    retained = recorded;
    retained.generation ^= 1;
    assert(!consume_vm_fault_boot_receipt(retained, true, result));
    retained = recorded;
    retained.code ^= 1;
    assert(!consume_vm_fault_boot_receipt(retained, true, result));

    auto longest = std::string(jellyframe::kDeviceMaxAppIdBytes, 'a');
    retained = make_vm_fault_boot_receipt(longest, 193, 120);
    assert(consume_vm_fault_boot_receipt(retained, true, result));
    assert(std::string_view(result.app_id.data()) == longest);
    longest.push_back('b');
    retained = make_vm_fault_boot_receipt(longest, 193, 120);
    assert(!consume_vm_fault_boot_receipt(retained, true, result));
    retained = make_vm_fault_boot_receipt(app_id, 193, -1);
    assert(!consume_vm_fault_boot_receipt(retained, true, result));
}
