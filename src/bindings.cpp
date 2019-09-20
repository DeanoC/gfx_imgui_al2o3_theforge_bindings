#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"
#include "gfx_imgui/imgui.h"

enum InputIds {
	MouseX,
	MouseY,
	MouseLeftClick,
	MouseRightClick,
};

struct ImguiBindings_Context {
	TheForge_RendererHandle renderer;
	ShaderCompiler_ContextHandle shaderCompiler;

	InputBasic_ContextHandle input;
	uint32_t userIdBlock;
	InputBasic_MouseHandle mouse;

	uint32_t maxDynamicUIUpdatesPerBatch;
	uint32_t maxFrames;

	uint32_t currentFrame;

	bool sharedState;
	TheForge_SamplerHandle bilinearSampler;
	TheForge_BlendStateHandle blendState;
	TheForge_DepthStateHandle depthState;
	TheForge_RasterizerStateHandle rasterizationState;

	TheForge_ShaderHandle shader;
	TheForge_RootSignatureHandle rootSignature;
	TheForge_PipelineHandle pipeline;
	TheForge_DescriptorBinderHandle descriptorBinder;
	TheForge_BufferHandle vertexBuffer;
	TheForge_BufferHandle indexBuffer;
	TheForge_BufferHandle uniformBuffer;

	ImguiBindings_Texture fontTexture;

	float scaleOffsetMatrix[16];

	ImGuiContext *context;
};

static const uint64_t UNIFORM_BUFFER_SIZE_PER_FRAME = 256;

static bool CreateShaders(ImguiBindings_Context *ctx) {
	static char const *const VertexShader = "cbuffer uniformBlockVS : register(b0, space0)\n"
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
																						"Texture2D colourTexture : register(t1, space1);\n"
																						"SamplerState bilinearSampler : register(s1, space2);\n"
																						"float4 FS_main(FSInput input) : SV_Target\n"
																						"{\n"
																						"\treturn input.Colour * colourTexture.Sample(bilinearSampler, input.Uv);\n"
																						"}\n";

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
		LOGWARNING("Shader compiler : %s %s", vokay ? "warnings" : "ERROR", vout.log);
	}
	ShaderCompiler_Output fout;
	bool fokay = ShaderCompiler_Compile(
			ctx->shaderCompiler, ShaderCompiler_ST_FragmentShader,
			"ImguiBindings_FragmentShader", fragEntryPoint, ffile,
			&fout);
	if (fout.log != nullptr) {
		LOGWARNING("Shader compiler : %s %s", fokay ? "warnings" : "ERROR", fout.log);
	}
	VFile_Close(vfile);
	VFile_Close(ffile);

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
	bdesc.vert.byteCode = (char *) vout.shader;
	bdesc.vert.byteCodeSize = (uint32_t) vout.shaderSize;
	bdesc.vert.entryPoint = vertEntryPoint;
	bdesc.frag.byteCode = (char *) fout.shader;
	bdesc.frag.byteCodeSize = (uint32_t) fout.shaderSize;
	bdesc.frag.entryPoint = fragEntryPoint;
	TheForge_AddShaderBinary(ctx->renderer, &bdesc, &ctx->shader);
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
	ctx->fontTexture.cpu = Image_CreateHeaderOnly(width, height, 1, 1, TinyImageFormat_R8G8B8A8_UNORM);

	TheForge_RawImageData rawData{
			pixels,
			TinyImageFormat_R8G8B8A8_UNORM,
			(uint32_t) width,
			(uint32_t) height,
			1,
			1,
			1
	};

	TheForge_TextureLoadDesc loadDesc{};
	loadDesc.pRawImageData = &rawData;
	loadDesc.pTexture = &ctx->fontTexture.gpu;
	loadDesc.mCreationFlag = TheForge_TCF_NONE;
	TheForge_LoadTexture(&loadDesc, false);
	if (!ctx->fontTexture.gpu)
		return false;

	ImGui::GetIO().Fonts->TexID = (void *) &ctx->fontTexture;

	return true;
}

static bool CreateRenderThings(ImguiBindings_Context *ctx,
															 ImguiBindings_Shared const *shared,
															 TinyImageFormat renderTargetFormat,
															 TheForge_SampleCount sampleCount,
															 uint32_t sampleQuality) {
	if (!CreateShaders(ctx))
		return false;
	if (!CreateFontTexture(ctx))
		return false;

	TheForge_VertexLayout const *vertexLayout = nullptr;
	if (!shared) {
		static TheForge_SamplerDesc const samplerDesc{
				TheForge_FT_LINEAR,
				TheForge_FT_LINEAR,
				TheForge_MM_LINEAR,
				TheForge_AM_CLAMP_TO_EDGE,
				TheForge_AM_CLAMP_TO_EDGE,
				TheForge_AM_CLAMP_TO_EDGE,
		};
		static TheForge_VertexLayout const staticVertexLayout{
				3,
				{
						{TheForge_SS_POSITION, 8, "POSITION", TinyImageFormat_R32G32_SFLOAT, 0, 0, 0},
						{TheForge_SS_TEXCOORD0, 9, "TEXCOORD", TinyImageFormat_R32G32_SFLOAT, 0, 1, sizeof(float) * 2},
						{TheForge_SS_COLOR, 5, "COLOR", TinyImageFormat_R8G8B8A8_UNORM, 0, 2, sizeof(float) * 4}
				}
		};
		static TheForge_BlendStateDesc const blendDesc{
				{TheForge_BC_SRC_ALPHA},
				{TheForge_BC_ONE_MINUS_SRC_ALPHA},
				{TheForge_BC_ONE},
				{TheForge_BC_ZERO},
				{TheForge_BM_ADD},
				{TheForge_BM_ADD},
				{0xF},
				TheForge_BST_0,
				false, false
		};
		static TheForge_DepthStateDesc const depthStateDesc{
				false, false,
				TheForge_CMP_ALWAYS,
		};
		static TheForge_RasterizerStateDesc const rasterizerStateDesc{
				TheForge_CM_NONE,
				0,
				0.0,
				TheForge_FM_SOLID,
				false,
				true,
		};

		TheForge_AddSampler(ctx->renderer, &samplerDesc, &ctx->bilinearSampler);
		TheForge_AddBlendState(ctx->renderer, &blendDesc, &ctx->blendState);
		TheForge_AddDepthState(ctx->renderer, &depthStateDesc, &ctx->depthState);
		TheForge_AddRasterizerState(ctx->renderer, &rasterizerStateDesc, &ctx->rasterizationState);
		vertexLayout = &staticVertexLayout;
		ctx->sharedState = false;
	} else {
		ctx->bilinearSampler = shared->bilinearSampler;
		ctx->blendState = shared->porterDuffBlendState;
		ctx->depthState = shared->ignoreDepthState;
		ctx->rasterizationState = shared->solidNoCullRasterizerState;
		vertexLayout = shared->twoD_PackedColour_UVVertexLayout;
		ctx->sharedState = true;
	}

	if (!ctx->bilinearSampler) {
		return false;
	}
	if (!ctx->blendState) {
		return false;
	}
	if (!ctx->depthState) {
		return false;
	}
	if (!ctx->rasterizationState) {
		return false;
	}

	static TheForge_BufferDesc const vbDesc{
			ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME * sizeof(ImDrawVert) * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			sizeof(ImDrawVert),
			0,
			0,
			0,
			nullptr,
			TinyImageFormat_UNDEFINED,
			TheForge_DESCRIPTOR_TYPE_VERTEX_BUFFER,
	};
	static TheForge_BufferDesc const ibDesc{
			ImguiBindings_MAX_INDEX_COUNT_PER_FRAME * sizeof(ImDrawIdx) * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			0,
			0,
			0,
			0,
			nullptr,
			TinyImageFormat_UNDEFINED,
			TheForge_DESCRIPTOR_TYPE_INDEX_BUFFER,
	};

	static TheForge_BufferDesc const ubDesc{
			UNIFORM_BUFFER_SIZE_PER_FRAME * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT |
																			TheForge_BCF_NO_DESCRIPTOR_VIEW_CREATION),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			0,
			0,
			0,
			0,
			nullptr,
			TinyImageFormat_UNDEFINED,
			TheForge_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	};


	TheForge_ShaderHandle shaders[]{ctx->shader};
	TheForge_SamplerHandle samplers[]{ctx->bilinearSampler};
	char const *staticSamplerNames[]{"bilinearSampler"};
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
	gfxPipeDesc.pVertexLayout = vertexLayout;
	gfxPipeDesc.blendState = ctx->blendState;
	gfxPipeDesc.depthState = nullptr;
	gfxPipeDesc.rasterizerState = ctx->rasterizationState;
	gfxPipeDesc.renderTargetCount = 1;
	gfxPipeDesc.pColorFormats = &renderTargetFormat;
	gfxPipeDesc.depthStencilFormat = TinyImageFormat_UNDEFINED;
	gfxPipeDesc.sampleCount = sampleCount;
	gfxPipeDesc.sampleQuality = sampleQuality;
	gfxPipeDesc.primitiveTopo = TheForge_PT_TRI_LIST;
	TheForge_AddPipeline(ctx->renderer, &pipelineDesc, &ctx->pipeline);
	if (!ctx->pipeline)
		return false;

	TheForge_DescriptorBinderDesc descriptorBinderDesc[] = {
			{ctx->rootSignature, ctx->maxDynamicUIUpdatesPerBatch},
			{ctx->rootSignature, ctx->maxDynamicUIUpdatesPerBatch},
	};
	TheForge_AddDescriptorBinder(ctx->renderer, 0, 2, descriptorBinderDesc, &ctx->descriptorBinder);
	if (!ctx->descriptorBinder)
		return false;

	TheForge_AddBuffer(ctx->renderer, &vbDesc, &ctx->vertexBuffer);
	if (!ctx->vertexBuffer)
		return false;
	TheForge_AddBuffer(ctx->renderer, &ibDesc, &ctx->indexBuffer);
	if (!ctx->indexBuffer)
		return false;
	TheForge_AddBuffer(ctx->renderer, &ubDesc, &ctx->uniformBuffer);
	if (!ctx->uniformBuffer)
		return false;

	return true;
}

static void DestroyRenderThings(ImguiBindings_Context *ctx) {
	if (ctx->fontTexture.gpu)
		TheForge_RemoveTexture(ctx->renderer, ctx->fontTexture.gpu);
	if (ctx->fontTexture.cpu)
		Image_Destroy(ctx->fontTexture.cpu);

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

	if (!ctx->sharedState) {
		if (ctx->rasterizationState) {
			TheForge_RemoveRasterizerState(ctx->renderer, ctx->rasterizationState);
		}
		if (ctx->depthState) {
			TheForge_RemoveDepthState(ctx->renderer, ctx->depthState);
		}
		if (ctx->blendState) {
			TheForge_RemoveBlendState(ctx->renderer, ctx->blendState);
		}
		if (ctx->bilinearSampler) {
			TheForge_RemoveSampler(ctx->renderer, ctx->bilinearSampler);
		}
	}

	if (ctx->shader)
		TheForge_RemoveShader(ctx->renderer, ctx->shader);
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
																																ImguiBindings_Shared const *shared,
																																uint32_t maxDynamicUIUpdatesPerBatch,
																																uint32_t maxFrames,
																																TinyImageFormat renderTargetFormat,
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

	if (!CreateRenderThings(ctx, shared, renderTargetFormat, sampleCount, sampleQuality)) {
		ImguiBindings_Destroy(ctx);
		return nullptr;
	}

	ImGuiIO &io = ImGui::GetIO();
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	ctx->userIdBlock = InputBasic_AllocateUserIdBlock(input);
	if (InputBasic_GetMouseCount(input) > 0) {

		ctx->mouse = InputBasic_MouseCreate(input, 0);
		InputBasic_MapToMouseAxis(ctx->input,
															ctx->userIdBlock + InputIds::MouseX,
															ctx->mouse, InputBasis_Axis_X);

		InputBasic_MapToMouseAxis(ctx->input,
															ctx->userIdBlock + InputIds::MouseY,
															ctx->mouse, InputBasis_Axis_Y);
		InputBasic_MapToMouseButton(ctx->input,
																ctx->userIdBlock + InputIds::MouseLeftClick,
																ctx->mouse, InputBasic_MouseButton_Left);

		InputBasic_MapToMouseButton(ctx->input,
																ctx->userIdBlock + (uint32_t) InputIds::MouseRightClick,
																ctx->mouse, InputBasic_MouseButton_Right);
	}

	return ctx;
}

AL2O3_EXTERN_C void ImguiBindings_Destroy(ImguiBindings_ContextHandle handle) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return;

	InputBasic_MouseDestroy(ctx->mouse);

	if (ctx->context)
		ImGui::DestroyContext(ctx->context);
	ctx->context = nullptr;

	DestroyRenderThings(ctx);

	MEMORY_FREE(ctx);
}

AL2O3_EXTERN_C void ImguiBindings_SetWindowSize(ImguiBindings_ContextHandle handle,
		uint32_t width,
		uint32_t height,
		float dpiBackingScaleX,	float dpiBackingScaleY ) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return;

	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize.x = (float) width;
	io.DisplaySize.y = (float) height;
	io.DisplayFramebufferScale.x = dpiBackingScaleX;
	io.DisplayFramebufferScale.y = dpiBackingScaleY;
}

AL2O3_EXTERN_C bool ImguiBindings_UpdateInput(ImguiBindings_ContextHandle handle, double deltaTimeInMS) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return false;
	ImGuiIO &io = ImGui::GetIO();
	io.DeltaTime = (float) deltaTimeInMS;

	io.MousePos.x = InputBasic_GetAsFloat(ctx->input, ctx->userIdBlock + InputIds::MouseX) * io.DisplaySize.x;
	io.MousePos.y = InputBasic_GetAsFloat(ctx->input, ctx->userIdBlock + InputIds::MouseY) * io.DisplaySize.y;
	io.MouseDown[0] = InputBasic_GetAsBool(ctx->input, ctx->userIdBlock + InputIds::MouseLeftClick);
	io.MouseDown[1] = InputBasic_GetAsBool(ctx->input, ctx->userIdBlock + InputIds::MouseRightClick);

	return io.WantCaptureMouse;
}

AL2O3_EXTERN_C uint32_t ImguiBindings_Render(ImguiBindings_ContextHandle handle, TheForge_CmdHandle cmd) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return 0;

	ImDrawData *drawData = ImGui::GetDrawData();

	// Copy and convert all vertices into a single contiguous buffer
	uint64_t const baseVertexOffset = ctx->currentFrame * ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME;
	uint64_t const baseIndexOffset = ctx->currentFrame * ImguiBindings_MAX_INDEX_COUNT_PER_FRAME;
	uint64_t const baseUniformOffset = ctx->currentFrame * UNIFORM_BUFFER_SIZE_PER_FRAME;

	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	for (int n = 0; n < drawData->CmdListsCount; n++) {
		// gather lists into single contigious buffers
		ImDrawList const *cmdList = drawData->CmdLists[n];

		TheForge_BufferUpdateDesc const vertexUpdate{
				ctx->vertexBuffer,
				cmdList->VtxBuffer.Data,
				0,
				baseVertexOffset + (vertexCount * sizeof(ImDrawVert)),
				cmdList->VtxBuffer.Size * sizeof(ImDrawVert)
		};
		TheForge_BufferUpdateDesc const indexUpdate{
				ctx->indexBuffer,
				cmdList->IdxBuffer.Data,
				0,
				baseIndexOffset + (indexCount * sizeof(ImDrawIdx)),
				(((cmdList->IdxBuffer.Size * sizeof(ImDrawIdx)) + 3u) & ~3u)
		};

		vertexCount += (uint32_t) cmdList->VtxBuffer.Size;
		indexCount += (uint32_t) cmdList->IdxBuffer.Size;

		if (vertexCount > ImguiBindings_MAX_VERTEX_COUNT_PER_FRAME)
			break;
		if (indexCount > ImguiBindings_MAX_INDEX_COUNT_PER_FRAME)
			break;

		TheForge_UpdateBuffer(&vertexUpdate, true);
		TheForge_UpdateBuffer(&indexUpdate, true);
	}

	TheForge_BufferBarrier barriers[] = {
			{ctx->vertexBuffer, TheForge_RS_VERTEX_AND_CONSTANT_BUFFER},
			{ctx->indexBuffer, TheForge_RS_INDEX_BUFFER},
	};

	TheForge_CmdResourceBarrier(cmd, 2, barriers, 0, nullptr);

	float const left = drawData->DisplayPos.x;
	float const right = drawData->DisplayPos.x + drawData->DisplaySize.x;
	float const top = drawData->DisplayPos.y;
	float const bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
	float const width = (right - left);
	float const height = (top - bottom);
	float const offX = (right + left) / (left - right);
	float const offY = (top + bottom) / (bottom - top);

	float const tmp[16]{
			2.0f / width, 0.0f, 0.0f, 0.0f,
			0.0f, 2.0f / height, 0.0f, 0.0f,
			0.0f, 0.0f, 0.5f, 0.0f,
			offX, offY, 0.5f, 1.0f,
	};
	memcpy(ctx->scaleOffsetMatrix, tmp, sizeof(float) * 16);

	TheForge_BufferUpdateDesc const constantsUpdate{
			ctx->uniformBuffer,
			ctx->scaleOffsetMatrix,
			0,
			baseUniformOffset,
			sizeof(float) * 16
	};
	TheForge_UpdateBuffer(&constantsUpdate, false);

	TheForge_CmdSetViewport(cmd, 0.0f, 0.0f,
													drawData->DisplaySize.x * drawData->FramebufferScale.x,
													drawData->DisplaySize.y  * drawData->FramebufferScale.y,
													0.0f, 1.0f);


	ImVec2 pos = drawData->DisplayPos;
	pos[0] *= drawData->FramebufferScale[0];
	pos[1] *= drawData->FramebufferScale[1];

	int lastVertexOffset = 0;
	int lastIndexOffset = 0;

	bool resetPipeline = true;
	ImguiBindings_Texture const* lastTexture = nullptr;

	for (int n = 0; n < drawData->CmdListsCount; n++) {
		const ImDrawList *cmdList = drawData->CmdLists[n];

		for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.size(); cmd_i++) {
			const ImDrawCmd *imcmd = &cmdList->CmdBuffer[cmd_i];
			if (imcmd->UserCallback) {
				if(imcmd->UserCallback == ImDrawCallback_ResetRenderState) {
					resetPipeline = true;
				}

				// User callback (registered via ImDrawList::AddCallback)

				// adjust the vertex and index offsets
				ImDrawCmd tmp;
				memcpy(&tmp, imcmd, sizeof(ImDrawCmd));
				tmp.IdxOffset = lastIndexOffset + imcmd->IdxOffset,
				tmp.VtxOffset = lastVertexOffset + imcmd->VtxOffset;
				imcmd->UserCallback(cmdList, &tmp);

				resetPipeline = true;
			} else {
				if(resetPipeline) {
					TheForge_CmdBindPipeline(cmd, ctx->pipeline);

					TheForge_DescriptorData params[1] = {};
					params[0].pName = "uniformBlockVS";
					params[0].pBuffers = &ctx->uniformBuffer;
					params[0].pOffsets = &baseUniformOffset;
					params[0].count = 1;
					TheForge_CmdBindDescriptors(cmd, ctx->descriptorBinder, ctx->rootSignature, 1, params);

					TheForge_CmdBindIndexBuffer(cmd, ctx->indexBuffer, baseIndexOffset);
					TheForge_CmdBindVertexBuffer(cmd, 1, &ctx->vertexBuffer, &baseVertexOffset);

					resetPipeline = false;
					lastTexture = nullptr;
				}
				float const clipX = imcmd->ClipRect.x * drawData->FramebufferScale.x;
				float const clipY = imcmd->ClipRect.y * drawData->FramebufferScale.y;
				float const clipZ = imcmd->ClipRect.z * drawData->FramebufferScale.x;
				float const clipW = imcmd->ClipRect.w * drawData->FramebufferScale.y;

				TheForge_CmdSetScissor(cmd,
															 (uint32_t) (clipX - pos.x),
															 (uint32_t) (clipY - pos.y),
															 (uint32_t) (clipZ - clipX),
															 (uint32_t) (clipW - clipY));

				ImguiBindings_Texture const
						*texture = imcmd->TextureId ? (ImguiBindings_Texture const *) imcmd->TextureId : nullptr;

				if(texture != lastTexture) {
					TheForge_DescriptorData params[2] = {};
					params[0].pName = "uniformBlockVS";
					params[0].pBuffers = &ctx->uniformBuffer;
					params[0].pOffsets = &baseUniformOffset;
					params[0].count = 1;
					params[1].pName = "colourTexture";
					params[1].pTextures = &(texture->gpu);
					params[1].count = 1;
					TheForge_CmdBindDescriptors(cmd, ctx->descriptorBinder, ctx->rootSignature, 2, params);
					lastTexture = texture;
				}

				TheForge_CmdDrawIndexed(cmd, imcmd->ElemCount,
																lastIndexOffset + imcmd->IdxOffset,
																lastVertexOffset + imcmd->VtxOffset);
			}
		}
		lastIndexOffset += cmdList->IdxBuffer.Size;
		lastVertexOffset += cmdList->VtxBuffer.Size;
	}
	uint32_t frameWeWroteTo = ctx->currentFrame;

	// where we will write next frame data, which was last used N frame ago
	ctx->currentFrame = (ctx->currentFrame + 1) % ctx->maxFrames;
	return frameWeWroteTo;
}

AL2O3_EXTERN_C float const *ImguiBindings_GetScaleOffsetMatrix(ImguiBindings_ContextHandle handle) {
	auto ctx = (ImguiBindings_Context *) handle;
	if (!ctx)
		return nullptr;

	return ctx->scaleOffsetMatrix;

}
