#pragma once

#include "render_core/event.h"
#include "render_core/feature_config.h"

// Policy is explicit, independent of which capabilities a Core happens to have.
// Direct consumers that do not use Runtime's CMake entry retain legacy behavior.
#ifndef JELLYFRAME_SCRIPT_R3_EVENTS
#define JELLYFRAME_SCRIPT_R3_EVENTS 0
#endif
#if JELLYFRAME_SCRIPT_R3_EVENTS != 0 && JELLYFRAME_SCRIPT_R3_EVENTS != 1
#error "JELLYFRAME_SCRIPT_R3_EVENTS must be 0 or 1"
#endif

#if JELLYFRAME_SCRIPT_R3_EVENTS
#if !defined(JELLYFRAME_EVENT_DISPATCH_COMPLETION_API) || JELLYFRAME_EVENT_DISPATCH_COMPLETION_API != 1
#error "R3 script events require Core dispatch completion API version 1"
#endif
#if !defined(JELLYFRAME_EVENT_DISPATCH_FAILURE_API) || JELLYFRAME_EVENT_DISPATCH_FAILURE_API != 1
#error "R3 script events require Core dispatch failure API version 1"
#endif
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
#if !defined(JELLYFRAME_EVENT_SUBMIT_KIND_API) || JELLYFRAME_EVENT_SUBMIT_KIND_API != 1
#error "R3 forms require Core Submit kind API version 1"
#endif
#endif
#elif JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED && !defined(__cpp_rtti) && !defined(_CPPRTTI)
#error "Legacy script forms require RTTI; select R3 with a capable Core or use a forms-disabled Core profile"
#endif

// Node identity has its own memory/dependency policy in either Event mode.
#ifndef JELLYFRAME_SCRIPT_NODE_IDENTITY
#define JELLYFRAME_SCRIPT_NODE_IDENTITY 0
#endif
#if JELLYFRAME_SCRIPT_NODE_IDENTITY != 0 && JELLYFRAME_SCRIPT_NODE_IDENTITY != 1
#error "JELLYFRAME_SCRIPT_NODE_IDENTITY must be 0 or 1"
#endif
