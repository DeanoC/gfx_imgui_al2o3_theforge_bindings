#pragma once
#ifndef GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_
#define GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_

#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"
#include "gfx_shadercompiler/compiler.h"
#include "input_basic/input.h"
#include "gfx_image/image.h"

static const uint64_t ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME = 1024 * 64;
static const uint64_t ImguiBindings_MAX_INDEX_COUNT_PER_FRAME = 128 * 1024;

typedef struct ImguiBindings_Texture {
	Image_ImageHeader const* cpu;
	TheForge_TextureHandle gpu;
} ImguiBindings_Texture;

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

AL2O3_EXTERN_C void ImguiBindings_SetWindowSize(ImguiBindings_ContextHandle handle, uint32_t width, uint32_t height, float backingScaleX, float backingScaleY);

AL2O3_EXTERN_C bool ImguiBindings_UpdateInput(ImguiBindings_ContextHandle handle, double deltaTimeInMS);

// returns the frame it just wrote data into, can be ignored except for custom rendering
AL2O3_EXTERN_C uint32_t ImguiBindings_Render(ImguiBindings_ContextHandle handle, TheForge_CmdHandle cmd);

// for custom rendering of imgui stuff (the buffeer has all frames in it, so you need to account for the offset)
AL2O3_EXTERN_C float const* ImguiBindings_GetScaleOffsetMatrix(ImguiBindings_ContextHandle handle);
AL2O3_EXTERN_C TheForge_BufferHandle ImguiBindings_GetVertexBuffer(ImguiBindings_ContextHandle handle);
AL2O3_EXTERN_C TheForge_BufferHandle ImguiBindings_GetIndexBuffer(ImguiBindings_ContextHandle handle);

#endif // end GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_