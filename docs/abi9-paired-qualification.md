# ABI9 Core and Runtime host qualification

The independent, manually dispatched ABI9 qualification takes a full Core
commit, its exact numeric version and the source SHA-256 from the independently
reviewed Core source manifest. The selected Runtime revision is the workflow
revision. The ordinary ABI4 workflow and production lock stay in place.

The matrix builds and installs Core from the supplied commit. It checks that
the checkout declares ABI9 and matches the supplied version and source hash;
it does not override either ABI or version at build time. Runtime consumes the
installed package using a generated, separate qualification lock. Full and
cropped enable contextual features; off keeps the full scalar feature profile
but disables contextual text, layout and JFSP2. Every profile runs the ordinary
signed frame13 host test, and full/cropped also run explicit signed frame12.
The host fixture uses a public test key and never provisions a device.

To run one profile locally, use a clean Runtime and Core checkout, CMake,
Clang and Ninja for Windows, Python 3.11 with `cryptography`, plus the pinned
JerryScript and mbedTLS sources listed in the workflow. Apply the Runtime
JerryScript patch and build it with `JERRY_VM_HALT=ON` as in the workflow.
Pass a fresh output directory outside the source checkouts:

```powershell
python project_tools/qualify_abi9_pair.py `
  --core $coreCheckout --core-commit $reviewedCoreCommit `
  --core-version $reviewedCoreVersion --core-source-hash $reviewedCoreSourceHash `
  --jerry $builtJerryCheckout --mbedtls $idfMbedtlsCheckout `
  --profile full --output $freshEvidenceDirectory
```

Keep the receipt and logs for all three profiles before treating a combination
as host qualified. An earlier receipt does not qualify a different commit or
source hash. Release artifact verification and physical WS147 acceptance
remain separate gates.
