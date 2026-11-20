# Core and Runtime paired host qualification

Last updated: 2026-11-20. Applies to: 0.6.0-dev.

The manually dispatched qualification takes a full Core commit, its exact
numeric version, its positive ABI (`core_abi`), and the source SHA-256 from the
independently reviewed Core source manifest. The selected Runtime revision is
the workflow revision. The qualification lock applies to these test consumers.
The workflow and scripts retain their historical names, and omission of the ABI
input selects ABI9. Pass `core_abi=11` in the workflow or `--core-abi 11`
on the command line for Core v0.12.0.

The matrix builds and installs Core from the supplied commit. The checkout,
installed package, qualification lock, and Runtime consumer provenance must
agree on the selected ABI, version and source hash. Core declares its own ABI
and version during the build. Full and
cropped enable contextual features; off keeps the full scalar feature profile
but disables contextual text, layout and JFSP2. Every profile executes the
ordinary signed host test (typed mainline frame10), and full/cropped also run
explicit signed frame12. Use `--scalar-frame 13` to qualify the experimental
Core, whose scalar command representation uses frame13. The probe verifies the
expected codec, and the signed test checks the actual selected route.
The host fixture uses a public test key and never provisions a device.

To run one profile locally, use a clean Runtime and Core checkout, CMake,
LLVM/Clang 22.1.8 (`clang-cl`) and Ninja for Windows from an MSVC developer
environment, Python 3.11 with `cryptography`, plus the pinned
JerryScript and mbedTLS sources listed in the workflow. Apply the Runtime
JerryScript patch and build it with `JERRY_VM_HALT=ON` as in the workflow.
Pass a fresh output directory outside the source checkouts:

```powershell
python project_tools/qualify_abi9_pair.py `
  --core $coreCheckout --core-commit $reviewedCoreCommit `
  --core-version $reviewedCoreVersion --core-abi $reviewedCoreAbi `
  --core-source-hash $reviewedCoreSourceHash `
  --jerry $builtJerryCheckout --mbedtls $idfMbedtlsCheckout `
  --profile full --output $freshEvidenceDirectory
```

When using `project_tools/qualify_contextual_installed_packages.py` directly,
give it the exact reviewed `--qualification-lock` and the same `--core-abi`.
All selected package profiles must match that lock. The production dependency
lock and the ABI4 compatibility job are independent of this qualification.

Keep the receipt and logs for all three profiles before treating a combination
as host qualified. An earlier receipt does not qualify a different commit or
source hash. Release artifact verification and physical WS147 acceptance
remain separate gates.
