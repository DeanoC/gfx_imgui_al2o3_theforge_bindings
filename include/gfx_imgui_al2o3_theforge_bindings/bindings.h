#pragma once
#ifndef GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_
#define GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_

#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"
#include "gfx_shadercompiler/compiler.h"
#include "input_basic/input.h"
#include "gfx_image/image.h"

static const uint64_t ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME = 1024 * 256;
static const uint64_t ImguiBindings_MAX_INDEX_COUNT_PER_FRAME = ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME * 3;

typedef struct ImguiBindings_Texture {
	Image_ImageHeader const* cpu;
	TheForge_TextureHandle gpu;
} ImguiBindings_Texture;

typedef struct ImguiBindings_Shared {
	TheForge_SamplerHandle bilinearSampler;
	TheForge_BlendStateHandle porterDuffBlendState;
	TheForge_DepthStateHandle ignoreDepthState;
	TheForge_RasterizerStateHandle solidNoCullRasterizerState;
	TheForge_VertexLayout const *twoD_PackedColour_UVVertexLayout;

} ImguiBindings_Shared;

typedef struct ImguiBindings_Context *ImguiBindings_ContextHandle;
AL2O3_EXTERN_C ImguiBindings_ContextHandle ImguiBindings_Create(TheForge_RendererHandle renderer,
																																ShaderCompiler_ContextHandle shaderCompiler,
																																InputBasic_ContextHandle input,
																																ImguiBindings_Shared const *shared, // can be null
																																uint32_t maxDynamicUIUpdatesPerBatch,
																																uint32_t maxFrames,
																																TinyImageFormat renderTargetFormat,
																																TheForge_SampleCount sampleCount,
																																uint32_t sampleQuality);
AL2O3_EXTERN_C void ImguiBindings_Destroy(ImguiBindings_ContextHandle handle);

AL2O3_EXTERN_C void ImguiBindings_SetWindowSize(ImguiBindings_ContextHandle handle, uint32_t width, uint32_t height, float backingScaleX, float backingScaleY);

AL2O3_EXTERN_C bool ImguiBindings_UpdateInput(ImguiBindings_ContextHandle handle, double deltaTimeInMS);

// returns the frame it just wrote data into, can be ignored except for custom rendering
AL2O3_EXTERN_C uint32_t ImguiBindings_Render(ImguiBindings_ContextHandle handle, TheForge_CmdHandle cmd);

AL2O3_EXTERN_C float const* ImguiBindings_GetScaleOffsetMatrix(ImguiBindings_ContextHandle handle);

#endif // end GFX_IMGUI_AL2O3_THEFORGE_BINDINGS_H_