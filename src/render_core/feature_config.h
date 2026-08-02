#pragma once

// Source-level defaults keep direct standalone compilation compatible with the
// default product profile. CMake overrides these values for a selected build.
#ifndef JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED
#define JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED 1
#endif

#ifndef JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
#define JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED 1
#endif

#ifndef JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
#define JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED 1
#endif

#ifndef JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
#define JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED 1
#endif
