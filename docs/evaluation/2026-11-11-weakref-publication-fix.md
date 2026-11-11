# JerryScript WeakRef property publication fix

Last updated: 2026-11-11; Applies to: 0.6.0-dev

The original Node-routing experiment supplied the engine and Runtime evidence
below. This delivery branch extracts only the dependency repair and necessary
IDF build fixes onto Runtime master; it contains no Node identity or R3 owner
implementation. Branch-specific validation belongs in
`2026-11-11-weakref-delivery-slice.md`.

The pinned JerryScript revision `b7069350c2e52e7dc721dfb75f067147bd79b39b`
publishes its internal `WEAK_REFS` property with an undefined value, then
assigns the collection pointer after property creation returns. Property
hashmap insertion inside creation can allocate and trigger GC. The GC marker
reads this internal property's value as a collection pointer, so collecting
during that publication window accesses an invalid collection.

The dependency patch creates the property with its collection pointer already
initialized. It uses the existing internal property creation logic and keeps
the existing collection owner. Node identity routing exercises this engine
path by constructing WeakRefs for wrappers with many properties.

## Applying the dependency patch

From this Runtime checkout, before building JerryScript:

```powershell
python tools/dependencies/patch_jerryscript.py --source-root E:/path/to/jerryscript --check
python tools/dependencies/patch_jerryscript.py --source-root E:/path/to/jerryscript
```

The tool checks normalized file hashes against the pinned inputs, applies
the patch atomically through `git apply`, and verifies the resulting hashes.
It recognizes an already patched tree and refuses partial or unknown inputs.
Rebuild all JerryScript libraries after applying it. All four desktop CI
JerryScript build steps apply the same patch. The IDF component copies these
three files into its binary directory, validates/applies the patch there, and
compiles the two patched C sources with the patched private header first in
its include search path. Changes to the patch/tool/manifest trigger configure.

## Evidence

Experiments and logs are in
`E:/Experiments/weakref-engine-isolation-20261111`.

- An independent engine-only harness creates a 40-property ordinary object
  and constructs a WeakRef. A test-only full GC at internal property
  publication crashes the unpatched build with exit `0xc0000005`. The patched
  build completes 100 iterations at the same collection boundary.
- Without the forced GC hook, swapping matching Debug engine libraries in the
  same Runtime test build makes the unpatched control fail and the patched
  build pass. The packaged patch on a clean engine copy also passes five
  consecutive complete script test runs. Runtime is ASan/UBSan instrumented,
  JerryScript core/ext are ASan instrumented, and the original platform port
  library remains uninstrumented.
- Both 40-property and 41-property publication-GC probes fail unpatched and
  pass 100 constructions patched, exercising both slots of a property pair.
  A WeakMap/WeakSet control also passes set/get/has across a full GC and
  deletion checks. Independent source review confirms collection ownership
  and the sweep protocol are preserved.
- Native MSVC Runtime Debug and Release script/worker groups each pass 1/1
  with the patched MSVC MinSizeRel engine in
  `E:/Builds/runtime-weakref-native-20261111`.
- Build `E:/Builds/runtime-weakref-fixed-asan-20261111` and log
  `runtime-packaged-asan-tests.log` cover the existing script runtime and
  worker groups, including the former Canvas reset-transform failure. The
  original wrapper construction scope is restored for these passing runs.
- Patch SHA-256 is
  `D8C1501299DCC3CBADBAE10DBA412A771EBACF787EBA1C5F756F398D095B218F`.
  Tested core library SHA-256 is
  `0531398DA9FDE4483DECC3DF0EC4792037B1F900D96AFCD2C592F22CE2B48234`.
  Script test executable SHA-256 is
  `A8B8A610295934D877F5955C54EBA1FA2151FA39D3C514FD02DCE35690CE7E29`.

The earlier attribution to Canvas setters, callback arity or the Event
construction scope was unsupported. Those changes alter allocation pressure
and collection timing. The engine-only publication test identifies a concrete
initialization defect, and the restored-scope Runtime test passes with the
engine patch.

## Target compilation and remaining qualification

Fresh ESP-IDF 5.3.1 build and subsequent build/size both pass in
`E:/Builds/r3-weakref-overlay-idf53-20261111`. The second build detects the
patch-tool change, reruns CMake, validates the overlay and succeeds. The
generated compile commands use the two patched C files and WeakRef remains
enabled. The caller's three original source files retain their original hashes.

The image is 5,991,952 bytes, compared with the earlier candidate's 5,991,760
bytes. SHA-256 is
`755E5AAF1114857529B987455B719BEA5A32E7622A542F146938C5E1BFE21271`.
Logs and build/source hashes are saved in
`E:/Experiments/weakref-engine-isolation-20261111/target-evidence`.
This establishes target compile/link qualification. Device heap/stack,
repeated-input and UI acceptance remain pending. Mainline delivery must also
resolve the separately audited Core R3 API and locked-package dependencies.
