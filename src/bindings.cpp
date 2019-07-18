#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"
#include "gfx_imgui/imgui.h"

struct ImguiBindings_Context {
	TheForge_RendererHandle renderer;
	ShaderCompiler_ContextHandle shaderCompiler;
	InputBasic_ContextHandle input;

	uint32_t maxDynamicUIUpdatesPerBatch;
	uint32_t maxFrames;

	uint32_t currentFrame;

	TheForge_ShaderHandle shader;
	TheForge_SamplerHandle bilinearSampler;
	TheForge_BlendStateHandle blendState;
	TheForge_RootSignatureHandle rootSignature;
	TheForge_PipelineHandle pipeline;
	TheForge_DepthStateHandle depthState;
	TheForge_RasterizerStateHandle rasterizationState;
	TheForge_DescriptorBinderHandle descriptorBinder;
	TheForge_BufferHandle vertexBuffer;
	TheForge_BufferHandle indexBuffer;
	TheForge_BufferHandle uniformBuffer;
	TheForge_TextureHandle fontTexture;

	ImGuiContext *context;
};

static const uint64_t MAX_VERTEX_COUNT_PER_FRAME = 1024 * 64;
static const uint64_t MAX_INDEX_COUNT_PER_FRAME = 128 * 1024;

static bool CreateShaders(ImguiBindings_Context *ctx) {
	static char const *const VertexShader = "cbuffer uniformBlockVS : register(b0)\n"
																					"{\n"
																					"\tfloat4x4 ProjectionMatrix;\n"
																					"};\n"
																					"struct VSInput\n"
																					"{\n"
																					"\tfloat2 Position : POSITION;\n"
																					"\tfloat2 Uv 			 : TEXCOORD0;\n"
																					"\tfloat4 Colour   : COLOR;\n"
																					"};\n"
																					"\n"
																					"struct VSOutput {\n"
																					"\tfloat4 Position : SV_POSITION;\n"
																					"\tfloat2 Uv 			 : TEXCOORD0;\n"
																					"\tfloat4 Colour   : COLOR;\n"
																					"};\n"
																					"\n"
																					"VSOutput VS_main(VSInput input)\n"
																					"{\n"
																					"    VSOutput result;\n"
																					"\n"
																					"\tresult.Position = mul(ProjectionMatrix, float4(input.Position, 0.f, 1.f));\n"
																					"\tresult.Uv = input.Uv;\n"
																					"\tresult.Colour = input.Colour;\n"
																					"\treturn result;\n"
																					"}";
	static char const *const FragmentShader = "struct FSInput {\n"
																						"\tfloat4 Position : SV_POSITION;\n"
																						"\tfloat2 Uv 			 : TEXCOORD;\n"
																						"\tfloat4 Colour   : COLOR;\n"
																						"};\n"
																						"\n"
																						"float4 FS_main(FSInput input) : SV_TARGET\n"
																						"{\n"
																						"\treturn float4(1,0,0,1);\n//input.Colour;\n"
																						"}";
	static char const *const vertEntryPoint = "VS_main";
	static char const *const fragEntryPoint = "FS_main";

	VFile_Handle vfile = VFile_FromMemory(VertexShader, strlen(VertexShader) + 1, false);
	if (!vfile)
		return false;
	VFile_Handle ffile = VFile_FromMemory(FragmentShader, strlen(FragmentShader) + 1, false);
	if (!ffile) {
		VFile_Close(vfile);
		return false;
	}

	ShaderCompiler_Output vout;
	bool vokay = ShaderCompiler_Compile(
			ctx->shaderCompiler, ShaderCompiler_ST_VertexShader,
			"ImguiBindings_VertexShader", vertEntryPoint, vfile,
			&vout);
	if (vout.log != nullptr) {
		LOGWARNINGF("Shader compiler : %s %s", vokay ? "warnings" : "ERROR", vout.log);
	}
	ShaderCompiler_Output fout;
	bool fokay = ShaderCompiler_Compile(
			ctx->shaderCompiler, ShaderCompiler_ST_FragmentShader,
			"ImguiBindings_FragmentShader", fragEntryPoint, ffile,
			&fout);
	if (fout.log != nullptr) {
		LOGWARNINGF("Shader compiler : %s %s", fokay ? "warnings" : "ERROR", fout.log);
	}
	VFile_Close(vfile);
	VFile_Close(ffile);

	LOGINFO((char*)vout.shader);
	LOGINFO((char*)fout.shader);

	if (!vokay || !fokay) {
		MEMORY_FREE((void *) vout.log);
		MEMORY_FREE((void *) vout.shader);
		MEMORY_FREE((void *) fout.log);
		MEMORY_FREE((void *) fout.shader);
		return false;
	}

#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
	TheForge_ShaderDesc sdesc;
	sdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	sdesc.vert.name = "ImguiBindings_VertexShader";
	sdesc.vert.code = (char *) vout.shader;
	sdesc.vert.entryPoint = vertEntryPoint;
	sdesc.vert.macroCount = 0;
	sdesc.frag.name = "ImguiBindings_FragmentShader";
	sdesc.frag.code = (char *) fout.shader;
	sdesc.frag.entryPoint = fragEntryPoint;
	sdesc.frag.macroCount = 0;
	TheForge_AddShader(ctx->renderer, &sdesc, &ctx->shader);
#else
	TheForge_BinaryShaderDesc bdesc;
	bdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	bdesc.vert.byteCode = (char*) vout.shader;
	bdesc.vert.byteCodeSize = (uint32_t)vout.shaderSize;
	bdesc.vert.entryPoint = vertEntryPoint;
	bdesc.vert.source = vtxt;
	bdesc.frag.byteCode = (char*) fout.shader;
	bdesc.frag.byteCodeSize = (uint32_t)fout.shaderSize;
	bdesc.frag.entryPoint = fragEntryPoint;
	bdesc.frag.source = ftxt;
	TheForge_AddShaderBinary(renderer, &bdesc, &shader);
#endif
	MEMORY_FREE((void *) vout.log);
	MEMORY_FREE((void *) vout.shader);
	MEMORY_FREE((void *) fout.log);
	MEMORY_FREE((void *) fout.shader);
	return true;
}

static bool CreateFontTexture(ImguiBindings_Context *ctx) {
	unsigned char *pixels;
	int width, height;
	ImGuiIO &io = ImGui::GetIO();
	io.Fonts->AddFontDefault();
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	TheForge_RawImageData rawData{
			pixels, TheForge_IF_RGBA8,
			(uint32_t) width,
			(uint32_t) height,
			1,
			1,
			1
	};

	TheForge_TextureLoadDesc loadDesc = {};
	loadDesc.pRawImageData = &rawData;
	loadDesc.pTexture = &ctx->fontTexture;
	loadDesc.mCreationFlag = TheForge_TCF_OWN_MEMORY_BIT;
	TheForge_LoadTexture(&loadDesc, false);
	if (!ctx->fontTexture)
		return false;

	return true;
}

static void DestroyRenderThings(ImguiBindings_Context *ctx) {
	if (ctx->fontTexture)
		TheForge_RemoveTexture(ctx->renderer, ctx->fontTexture);
	if (ctx->uniformBuffer)
		TheForge_RemoveBuffer(ctx->renderer, ctx->uniformBuffer);
	if (ctx->vertexBuffer)
		TheForge_RemoveBuffer(ctx->renderer, ctx->vertexBuffer);
	if (ctx->indexBuffer)
		TheForge_RemoveBuffer(ctx->renderer, ctx->indexBuffer);
	if (ctx->descriptorBinder)
		TheForge_RemoveDescriptorBinder(ctx->renderer, ctx->descriptorBinder);
	if (ctx->pipeline)
		TheForge_RemovePipeline(ctx->renderer, ctx->pipeline);
	if (ctx->rootSignature)
		TheForge_RemoveRootSignature(ctx->renderer, ctx->rootSignature);
	if (ctx->rasterizationState)
		TheForge_RemoveRasterizerState(ctx->renderer, ctx->rasterizationState);
	if (ctx->depthState)
		TheForge_RemoveDepthState(ctx->renderer, ctx->depthState);
	if (ctx->blendState)
		TheForge_RemoveBlendState(ctx->renderer, ctx->blendState);
	if (ctx->bilinearSampler)
		TheForge_RemoveSampler(ctx->renderer, ctx->bilinearSampler);
	if (ctx->shader)
		TheForge_RemoveShader(ctx->renderer, ctx->shader);
}

static bool CreateRenderThings(ImguiBindings_Context *ctx,
															 TheForge_ImageFormat renderTargetFormat,
															 TheForge_ImageFormat depthStencilFormat,
															 bool sRGB,
															 TheForge_SampleCount sampleCount,
															 uint32_t sampleQuality) {
	if (!CreateShaders(ctx))
		return false;
	if (!CreateFontTexture(ctx))
		return false;

	static TheForge_SamplerDesc const samplerDesc{
			TheForge_FT_LINEAR,
			TheForge_FT_LINEAR,
			TheForge_MM_LINEAR,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
	};
	static TheForge_VertexLayout const vertexLayout{
			3,
			{
					{TheForge_SS_POSITION, 8, "POSITION", TheForge_IF_RG32F, 0, 0, 0},
					{TheForge_SS_TEXCOORD0, 9, "TEXCOORD0", TheForge_IF_RG32F, 0, 1, sizeof(float) * 2},
					{TheForge_SS_COLOR, 5, "COLOR", TheForge_IF_RGBA8, 0, 2, sizeof(float) * 4}
			}
	};
	static TheForge_BlendStateDesc const blendDesc{
			{TheForge_BC_ONE},
			{TheForge_BC_ZERO},
			{TheForge_BC_ONE},
			{TheForge_BC_ZERO},
			{TheForge_BM_ADD},
			{TheForge_BM_ADD},
			{0xFFFFFFFF},
			TheForge_BST_0,
			false, false
	};
	static TheForge_DepthStateDesc const depthStateDesc{
			false, false
	};
	static TheForge_RasterizerStateDesc const rasterizerStateDesc{
			TheForge_CM_NONE,
			0,
			0.0,
			TheForge_FM_SOLID,
			false,
			false,
	};
	static TheForge_BufferDesc const vbDesc{
			MAX_VERTEX_COUNT_PER_FRAME * sizeof(ImDrawVert) * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT | TheForge_BCF_OWN_MEMORY_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			sizeof(ImDrawVert),
			0,
			0,
			0,
			nullptr,
			TheForge_IF_NONE,
			TheForge_DESCRIPTOR_TYPE_VERTEX_BUFFER,
	};
	static TheForge_BufferDesc const ibDesc{
			MAX_INDEX_COUNT_PER_FRAME * sizeof(ImDrawIdx) * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT | TheForge_BCF_OWN_MEMORY_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			0,
			0,
			0,
			0,
			nullptr,
			TheForge_IF_NONE,
			TheForge_DESCRIPTOR_TYPE_INDEX_BUFFER,
	};

	static TheForge_BufferDesc const ubDesc{
			sizeof(float) * 16 * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT | TheForge_BCF_OWN_MEMORY_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			0,
			0,
			0,
			0,
			nullptr,
			TheForge_IF_NONE,
			TheForge_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	};

	TheForge_AddSampler(ctx->renderer, &samplerDesc, &ctx->bilinearSampler);
	if (!ctx->bilinearSampler)
		return false;
	TheForge_AddBlendState(ctx->renderer, &blendDesc, &ctx->blendState);
	if (!ctx->blendState)
		return false;
	TheForge_AddDepthState(ctx->renderer, &depthStateDesc, &ctx->depthState);
	if (!ctx->depthState)
		return false;
	TheForge_AddRasterizerState(ctx->renderer, &rasterizerStateDesc, &ctx->rasterizationState);
	if (!ctx->rasterizationState)
		return false;

	TheForge_ShaderHandle shaders[]{ctx->shader};
	TheForge_SamplerHandle samplers[]{ctx->bilinearSampler};
	char const *staticSamplerNames[]{"BilinearSampler"};

	TheForge_RootSignatureDesc rootSignatureDesc{};
	rootSignatureDesc.shaderCount = 1;
	rootSignatureDesc.pShaders = shaders;
	rootSignatureDesc.staticSamplerCount = 1;
	rootSignatureDesc.pStaticSamplerNames = staticSamplerNames;
	rootSignatureDesc.pStaticSamplers = samplers;
	TheForge_AddRootSignature(ctx->renderer, &rootSignatureDesc, &ctx->rootSignature);
	if (!ctx->rootSignature)
		return false;

	TheForge_PipelineDesc pipelineDesc{};
	pipelineDesc.type = TheForge_PT_GRAPHICS;
	TheForge_GraphicsPipelineDesc &gfxPipeDesc = pipelineDesc.graphicsDesc;
	gfxPipeDesc.shaderProgram = ctx->shader;
	gfxPipeDesc.rootSignature = ctx->rootSignature;
	gfxPipeDesc.pVertexLayout = &vertexLayout;
	gfxPipeDesc.blendState = ctx->blendState;
	gfxPipeDesc.depthState = ctx->depthState;
	gfxPipeDesc.rasterizerState = ctx->rasterizationState;
	gfxPipeDesc.renderTargetCount = 1;
	gfxPipeDesc.pColorFormats = &renderTargetFormat;
	gfxPipeDesc.depthStencilFormat = depthStencilFormat;
	gfxPipeDesc.pSrgbValues = &sRGB;
	gfxPipeDesc.sampleCount = sampleCount;
	gfxPipeDesc.sampleQuality = sampleQuality;
	gfxPipeDesc.primitiveTopo = TheForge_PT_TRI_LIST;
	TheForge_AddPipeline(ctx->renderer, &pipelineDesc, &ctx->pipeline);
	if (!ctx->pipeline)
		return false;

	TheForge_DescriptorBinderDesc descriptorBinderDesc = {
			ctx->rootSignature,
			ctx->maxDynamicUIUpdatesPerBatch
	};
	TheForge_AddDescriptorBinder(ctx->renderer, 0, 1, &descriptorBinderDesc, &ctx->descriptorBinder);
	if (!ctx->descriptorBinder)
		return false;

	TheForge_AddBuffer(ctx->renderer, &vbDesc, &ctx->vertexBuffer);
	if (!ctx->vertexBuffer)
		return false;
	TheForge_AddBuffer(ctx->renderer, &ibDesc, &ctx->indexBuffer);
	if (!ctx->indexBuffer)
		return false;
	TheForge_AddBuffer(ctx->renderer, &ibDesc, &ctx->uniformBuffer);
	if (!ctx->uniformBuffer)
		return false;

	return true;
}

static void *alloc_func(size_t sz, void *user_data) {
	return MEMORY_MALLOC(sz);
}

static void free_func(void *ptr, void *user_data) {
	MEMORY_FREE(user_data);
}

AL2O3_EXTERN_C ImguiBindings_ContextHandle ImguiBindings_Create(TheForge_RendererHandle renderer,
																																ShaderCompiler_ContextHandle shaderCompiler,
																																InputBasic_ContextHandle input,
																																uint32_t maxDynamicUIUpdatesPerBatch,
																																uint32_t maxFrames,
																																TheForge_ImageFormat renderTargetFormat,
																																TheForge_ImageFormat depthStencilFormat,
																																bool sRGB,
																																TheForge_SampleCount sampleCount,
																																uint32_t sampleQuality) {
	auto ctx = (ImguiBindings_Context *) MEMORY_CALLOC(1, sizeof(ImguiBindings_Context));
	if (!ctx)
		return nullptr;

	ctx->renderer = renderer;
	ctx->shaderCompiler = shaderCompiler;
	ctx->input = input;
	ctx->maxDynamicUIUpdatesPerBatch = maxDynamicUIUpdatesPerBatch;
	ctx->maxFrames = maxFrames;

	ImGui::SetAllocatorFunctions(alloc_func, free_func);
	ctx->context = ImGui::CreateContext();
	ImGui::SetCurrentContext(ctx->context);

	if (!CreateRenderThings(ctx,
													renderTargetFormat, depthStencilFormat, sRGB, sampleCount, sampleQuality)) {
		ImguiBindings_Destroy(ctx);
		return nullptr;
	}

	return ctx;
}

AL2O3_EXTERN_C void ImguiBindings_Destroy(ImguiBindings_ContextHandle handle) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return;

	if (ctx->context)
		ImGui::DestroyContext(ctx->context);
	ctx->context = nullptr;

	DestroyRenderThings(ctx);

	MEMORY_FREE(ctx);
}

AL2O3_EXTERN_C void ImguiBindings_SetWindowSize(ImguiBindings_ContextHandle handle, uint32_t width, uint32_t height) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return;

	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize.x = width;
	io.DisplaySize.y = height;
}

AL2O3_EXTERN_C void ImguiBindings_SetDeltaTime(ImguiBindings_ContextHandle handle, double deltaTimeInMS) {
	ImGuiIO &io = ImGui::GetIO();
	io.DeltaTime = (float) deltaTimeInMS;
}

AL2O3_EXTERN_C void ImguiBindings_Draw(ImguiBindings_ContextHandle handle, TheForge_CmdHandle cmd) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return;

	// we are writing next frame data, which was last used N frame ago
	ctx->currentFrame = (ctx->currentFrame + 1) % ctx->maxFrames;

	ImGui::Render();
	ImDrawData *drawData = ImGui::GetDrawData();
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;

	for (int n = 0; n < drawData->CmdListsCount; n++) {
		const ImDrawList *cmd_list = drawData->CmdLists[n];
		vertexCount += (uint32_t) cmd_list->VtxBuffer.size();
		indexCount += (uint32_t) cmd_list->IdxBuffer.size();
	}
	if (vertexCount > MAX_VERTEX_COUNT_PER_FRAME)
		vertexCount = MAX_VERTEX_COUNT_PER_FRAME;
	if (indexCount > MAX_INDEX_COUNT_PER_FRAME)
		indexCount = MAX_INDEX_COUNT_PER_FRAME;

	// Copy and convert all vertices into a single contiguous buffer
	uint64_t vertexOffset = ctx->currentFrame * MAX_VERTEX_COUNT_PER_FRAME * sizeof(ImDrawVert);
	uint64_t indexOffset = ctx->currentFrame * MAX_INDEX_COUNT_PER_FRAME * sizeof(ImDrawIdx);

	for (int n = 0; n < drawData->CmdListsCount; n++) {
		// gather lists into single contigious buffers
		ImDrawList const *cmdList = drawData->CmdLists[n];
		TheForge_BufferUpdateDesc const vertexUpdate{
				ctx->vertexBuffer,
				cmdList->VtxBuffer.Data,
				0,
				vertexOffset,
				cmdList->VtxBuffer.Size * sizeof(ImDrawVert)
		};
		TheForge_BufferUpdateDesc const indexUpdate{
				ctx->indexBuffer,
				cmdList->IdxBuffer.Data,
				0,
				indexOffset,
				cmdList->IdxBuffer.Size * sizeof(ImDrawIdx)
		};

		TheForge_UpdateBuffer(&vertexUpdate, true);
		TheForge_UpdateBuffer(&indexUpdate, true);

		vertexOffset += cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
		indexOffset += cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
	}

	float const left = drawData->DisplayPos.x;
	float const right = drawData->DisplayPos.x + drawData->DisplaySize.x;
	float const top = drawData->DisplayPos.y;
	float const bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
	float const width = right - left;
	float const height = top - bottom;
	float const offX = (right + left) / (left - right);
	float const offY = (top + bottom) / (bottom - top);
	float transform[4][4] = {
			{2.0f / width, 0.0f, 0.0f, 0.0f},
			{0.0f, 2.0f / height, 0.0f, 0.0f},
			{0.0f, 0.0f, 0.5f, 0.0f},
			{offX, offY, 0.5f, 1.0f},
	};
	TheForge_BufferUpdateDesc const constantsUpdate{
			ctx->uniformBuffer,
			transform,
			0,
			ctx->currentFrame * sizeof(transform),
			sizeof(transform)
	};
	TheForge_UpdateBuffer(&constantsUpdate, true);

	TheForge_CmdSetViewport(cmd, 0.0f, 0.0f, drawData->DisplaySize.x, drawData->DisplaySize.y, 0.0f, 1.0f);
	TheForge_CmdSetScissor(
			cmd, (uint32_t) drawData->DisplayPos.x, (uint32_t) drawData->DisplayPos.y, (uint32_t) drawData->DisplaySize.x,
			(uint32_t) drawData->DisplaySize.y);
	TheForge_CmdBindPipeline(cmd, ctx->pipeline);

	uint64_t const cmdVertexOffset = ctx->currentFrame * MAX_VERTEX_COUNT_PER_FRAME;
	uint64_t const cmdUniformOffset = ctx->currentFrame * sizeof(float) * 16;
	TheForge_CmdBindIndexBuffer(cmd, ctx->indexBuffer, ctx->currentFrame * MAX_INDEX_COUNT_PER_FRAME);
	TheForge_CmdBindVertexBuffer(cmd, 1, &ctx->vertexBuffer, &cmdVertexOffset);

	TheForge_DescriptorData params[1] = {};
	params[0].pName = "uniformBlockVS";
	params[0].pOffsets = &cmdUniformOffset;
	params[0].pBuffers = &ctx->uniformBuffer;
	TheForge_CmdBindDescriptors(cmd, ctx->descriptorBinder, ctx->rootSignature, 1, params);

	int vtx_offset = 0;
	int idx_offset = 0;
	ImVec2 pos = drawData->DisplayPos;
	for (int n = 0; n < drawData->CmdListsCount; n++) {
		const ImDrawList *cmdList = drawData->CmdLists[n];
		for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.size(); cmd_i++) {
			const ImDrawCmd *imcmd = &cmdList->CmdBuffer[cmd_i];
			if (imcmd->UserCallback) {
				// User callback (registered via ImDrawList::AddCallback)
				imcmd->UserCallback(cmdList, imcmd);
			} else {
				TheForge_CmdSetScissor(cmd,
															 (uint32_t) (imcmd->ClipRect.x - pos.x),
															 (uint32_t) (imcmd->ClipRect.y - pos.y),
															 (uint32_t) (imcmd->ClipRect.z - imcmd->ClipRect.x),
															 (uint32_t) (imcmd->ClipRect.w - imcmd->ClipRect.y));

				TheForge_DescriptorData params[1] = {};
				params[0].pName = "uTex";
				params[0].pTextures = imcmd->TextureId ? &ctx->fontTexture : nullptr;

				TheForge_CmdBindDescriptors(cmd, ctx->descriptorBinder, ctx->rootSignature, 1, params);
				TheForge_CmdDrawIndexed(cmd, imcmd->ElemCount, idx_offset, vtx_offset);
			}
			idx_offset += imcmd->ElemCount;
		}
		vtx_offset += (int) cmdList->VtxBuffer.size();
	}
}