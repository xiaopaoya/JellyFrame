# R3 Runtime delivery checkpoint

Last updated: 2026-11-12; Applies to: 0.6.0-dev

The delivery branch starts at Runtime master `c57d731f`. It brings shared
backend State, bounded operation/owner pins, worker session retention and checked
Event publication through the ordinary Runtime and installed-consumer entries.
`JELLYFRAME_ENABLE_R3_SCRIPT_EVENTS` defaults to OFF. Enabling it requires Core
completion and failure API version 1 and, with forms, Submit kind version 1.
The same capability header validates desktop, standalone consumers and IDF.

Both listener registration routes use one invocation helper. Callback admission
starts before Event/Node wrapper construction and rejects document replacement
through staging and execution. Legacy Event objects have callback-local data
snapshots and RTTI-based Submit identification. R3 retains one Event object and
session owner until native dispatch completion and reports infrastructure
failure independently of cancelability. Legacy native callers retain borrowed
document/session resources across the entire dispatch and consume the host
failure latch. Its callback pins cannot observe gaps between native listeners.

Review corrected a refused owner-pin lifetime issue by retaining backend State
through refusal diagnostics. A regression deletes the real facade inside a
denied retain hook and verifies balanced admitted pins and subsequent VM reuse.
Legacy preventDefault writes its snapshot field. Worker refusal tests exercise
operation admission in legacy and shared Event capacity in R3.

## Current verification

| Configuration | Result |
| --- | --- |
| Locked released Core0.8 ABI3, legacy, MSVC Release complete Runtime CTest | 21/21 |
| New Core, R3, MSVC Release complete Runtime CTest | 21/21 |
| Fixed F5 full/cropped v10 codec and worker | 2/2 each |
| Fixed F5 full/cropped v9 script API and worker consumers | 4/4 each |
| New Core full/cropped, legacy and R3, v9 consumers | 4/4 in all four combinations |
| Legacy ABI3 and new Core R3, ASan+UBSan combined script/worker group | 1/1 each |
| Actual no-RTTI full R3 installed consumers | 4/4 |

The ABI3 generated provenance has lockEnforced=true and source
`fa5914ce1559da5b12522fa28775287a535543d1be64da19a985e3d3f84cd795` /111.
Fixed F5 uses `a0067eeb`, source
`369973a9b051cdff8af56f5bd11a94cd17c75de6940473d2fa6dcee8b171c33c` /139.
New Core is merged `f7aee11c`, source
`f7c17009b7660a84ef6a492367637187dff6915c4af3418c769902b3b7eb2559` /145.
Full is render-core-default and cropped is render-core-minimal-no-forms-advanced.

The capability compile matrix exercises 18 combinations including omitted
policy, missing/zero/future API versions, forms and RTTI. An actual ABI3
configure with R3 ON rejects. Real no-RTTI consumer commands and preprocessed
tests confirm enabled assertions, R3/forms and executed typed Submit, generic
same-name Event, null submitter and staging-admission checks. Independent review
found no further confirmed adapter/lifetime defect.

Sanitizer runs instrument Runtime C++ and JerryScript core/ext. The ABI3 Core
package was rebuilt with compatible annotations and new-Core source builds are
instrumented. Platform Jerry port remains uninstrumented. Leak detection is
disabled. The complete Runtime configurations above disable Core's own tests.

## Target compilation

Fresh ESP-IDF5.3.1 script/task build and size succeed in both modes. Actual
bridge commands contain -fno-rtti and exclude the staging test hook. Both use
the new Core source above. R3 enables forms and legacy disables forms.

| Mode | BIN bytes | SHA256 |
| --- | ---: | --- |
| R3/forms | 5,983,376 | `9efb69b07eabf1dab20727cb80c7f02f6cd48042c520b993f8040dcb5759c0e0` |
| Legacy/forms disabled | 5,957,040 | `923263b0c8994077aa607f769f61e6aa60c875f96e2acf2d1df85e67b0596628` |

Device execution, heap/stack watermarks and UI lifecycle acceptance remain
pending. Logs, runners, exact flags and independent review are under
`E:/Experiments/runtime-r3-delivery-20261112`. Earlier failure logs remain
historical intermediate evidence. CI adds pinned new-Core full/cropped R3
consumers with RTTI disabled, retains legacy package jobs and adds actual
script API coverage to the fixed F5 jobs. Remote CI is the next delivery gate.
