#pragma once
#ifndef GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_
#define GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_

#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"
#include "gfx_shadercompiler/compiler.h"
#include "input_basic/input.h"

typedef struct ImguiBindings_Context *ImguiBindings_ContextHandle;
AL2O3_EXTERN_C ImguiBindings_ContextHandle ImguiBindings_Create(TheForge_RendererHandle renderer,
																																ShaderCompiler_ContextHandle shaderCompiler,
																																InputBasic_ContextHandle input,
																																uint32_t maxDynamicUIUpdatesPerBatch,
																																uint32_t maxFrames,
																																TheForge_ImageFormat renderTargetFormat,
																																TheForge_ImageFormat depthStencilFormat,
																																bool sRGB,
																																TheForge_SampleCount sampleCount,
																																uint32_t sampleQuality);
AL2O3_EXTERN_C void ImguiBindings_Destroy(ImguiBindings_ContextHandle handle);

AL2O3_EXTERN_C void ImguiBindings_SetWindowSize(ImguiBindings_ContextHandle handle, uint32_t width, uint32_t height);

AL2O3_EXTERN_C bool ImguiBindings_UpdateInput(ImguiBindings_ContextHandle handle, double deltaTimeInMS);
AL2O3_EXTERN_C void ImguiBindings_Draw(ImguiBindings_ContextHandle handle, TheForge_CmdHandle cmd);

#endif // end GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_