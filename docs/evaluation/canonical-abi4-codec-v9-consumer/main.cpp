#include "app_runtime/script_task_canonical_codec.h"
static_assert(jellyframe::detail::canonical::available, "Installed Core must expose the actual canonical v9 prerequisites");
int script_task_frame_codec_tests_main();
int script_task_frame_renderer_tests_main();
int main() {
  if (script_task_frame_codec_tests_main() != 0) return 1;
  return script_task_frame_renderer_tests_main();
}
