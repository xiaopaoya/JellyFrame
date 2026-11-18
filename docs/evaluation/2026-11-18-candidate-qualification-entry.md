# Active-source candidate qualification and native image continuation

Last updated: 2026-11-18; Applies to: 0.6.0-dev.

## One selected package identity

CMake accepts an explicit JELLYFRAME_RENDER_CORE_QUALIFICATION_LOCK_FILE for
the package provider. The selected file must declare a complete version, ABI
and SHA256 source identity. Cache values are cleared before reading it.
Package version, ABI, source manifest and imported archive checks remain active.
Provenance records lockKind, normalized lockFile and lockFileSha256.
The normal repository lock continues to define production tool metadata.

Candidate Desktop qualification now builds the active Git checkout directly.
Candidate sample manifests are generated in the build tree and paired with a
test-local version environment. The remaining seven CLI checks, template trial
and external doctor now use that pairing. Source manifest/document consistency
tests continue reading the repository's production inputs. The installed
contextual qualifier uses a small add_subdirectory harness and the same explicit
lock option. It no longer copies Runtime or patches a provider include string.

Nine focused tests cover default/candidate lock selection and switching, exact
version/ABI/hash refusal, missing/incomplete locks, provider selection, staging
source preservation and CTest command/fixture/environment pairing. Production
and candidate source preflight suites pass. Out-of-tree trial output cleanup
verifies CMake source ownership and strict containment within the selected build
root. Two regressions cover a separate build root and an unrelated checkout.

## SDK identity and actual native smoke

SDK packaging validates generated build provenance before copying tools. An
explicit qualification SDK uses the same reviewed candidate lock, stages its
own template versions and records Core identity in the SDK manifest. Normal
in-tree/source-override development builds retain their existing workflow,
with development identity and matching generated lock/templates inside the SDK.
Normal and scripting profiles must have the same Core version, ABI, source hash
and source count.

The unpacked SDK test clears the candidate test environment, creates and validates
an app, then runs the bundled native CLI pipeline and checks actual display
commands. Wrong version, ABI or source hash refuses before archive creation.
Candidate ABI9 and source-override ABI4 SDK tests pass. Shared capability-tool
import tests pass17/17. Build provenance and this smoke are host artifact checks.
They do not establish signed distribution or physical installation trust.

## Weather source switches

After Core admitted definite image dimension hints, Weather reached its first
interactive image change and failed required image resolution. Desktop had
registered mock decoder inputs only for URLs present at initial load. Dynamic
src URLs therefore completed with404 despite existing package resources.

Desktop now retains the package resource context and registers newly selected
content images before the synchronous Core preparation phase. URL registration
is deduplicated and reset with image services on app rebuild. URL bytes and
retained decoded fixture bytes each use the existing1MiB native input ceiling,
with the package resource-entry ceiling on distinct URLs. Ready surfaces retain
the existing eight-entry/512KiB cache. Package bundle storage remains owned for
this host route, so its retained memory still needs resource qualification.

The actual Weather frame script now passes with its original three image
completions, network completion and interactions. Native image replacement and
package image tests also pass. ABI4 targeted package/preflight/SDK/image/Weather
tests pass6/6 including fixture setup.

## Current gate

The active-source Desktop build now uses Core8ac70baf, source641/hashf431075e,
full archive8d7de78599857e0190574e49acda5921899f2009c06a022c9ff2b50bd5cb17fd.
The Core-owned native-size correction closes calculator strict validation on all
three targets and the unchanged template trial. Its broad Runtime suite now
passes74/88 with14 failed gates. Remaining failures include font-policy repertoire, Canvas/motion layout,
incremental/counter expectations and dependent aggregate trials. The external
trial now reaches the actual Canvas failure. No acceptance assertion was
weakened to close these product gates.

Evidence directories are
E:/Experiments/migration-candidate-qualification-entry-20261118 and
E:/Experiments/migration-native-control-sizing-20261118. The current active-source
log is the latter directory's runtime-full-ctest.log. Earlier snapshot62/87 and
active-source72/88 and73/88 runs remain historical.
Installed Runtime full/cropped/contextual-OFF now each pass
39/39 CTests against this same Core identity. The wrapper probe, imported archive,
public headers, active assertions and absence of bundled Core source are audited.
Runtime inputs remain unchanged during the matrix. Receipt is
E:/Experiments/migration-contextual-installed-f431075e-20261118/result.json.
Sample/Desktop and signed execution are separately configured scopes. Complete
signed/resource/target carry-forward and physical acceptance remain open.
