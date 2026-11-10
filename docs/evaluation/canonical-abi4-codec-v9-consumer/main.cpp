#include "app_runtime/script_task_canonical_codec.h"
#include <cstdlib>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
static_assert(jellyframe::detail::canonical::available, "Installed Core must expose the actual canonical v9 prerequisites");
int script_task_frame_codec_tests_main();
int script_task_frame_renderer_tests_main();
int main() {
#if defined(_MSC_VER)
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  if (script_task_frame_codec_tests_main() != 0) return 1;
  return script_task_frame_renderer_tests_main();
}
