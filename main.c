#include <SDL3/SDL.h>

SDL_GPUShader* LoadShader(
	SDL_GPUDevice* device,
	const char* shaderFilename,
	Uint32 samplerCount,
	Uint32 uniformBufferCount,
	Uint32 storageBufferCount,
	Uint32 storageTextureCount
) {

	const char* BasePath = SDL_GetBasePath();


	// Auto-detect the shader stage from the file name for convenience
	SDL_GPUShaderStage stage;
	if (SDL_strstr(shaderFilename, ".vert"))
	{
		stage = SDL_GPU_SHADERSTAGE_VERTEX;
	}
	else if (SDL_strstr(shaderFilename, ".frag"))
	{
		stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	}
	else
	{
		SDL_Log("Invalid shader stage!");
		return NULL;
	}

	char fullPath[256];
	SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
	const char *entrypoint;

	if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
		SDL_snprintf(fullPath, sizeof(fullPath), "%sContent/Shaders/Compiled/SPIRV/%s.spv", BasePath, shaderFilename);
		format = SDL_GPU_SHADERFORMAT_SPIRV;
		entrypoint = "main";
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
		SDL_snprintf(fullPath, sizeof(fullPath), "%sContent/Shaders/Compiled/MSL/%s.msl", BasePath, shaderFilename);
		format = SDL_GPU_SHADERFORMAT_MSL;
		entrypoint = "main0";
	} else if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
		SDL_snprintf(fullPath, sizeof(fullPath), "%sContent/Shaders/Compiled/DXIL/%s.dxil", BasePath, shaderFilename);
		format = SDL_GPU_SHADERFORMAT_DXIL;
		entrypoint = "main";
	} else {
		SDL_Log("%s", "Unrecognized backend shader format!");
		return NULL;
	}

	size_t codeSize;
	void* code = SDL_LoadFile(fullPath, &codeSize);
	if (code == NULL)
	{
		SDL_Log("Failed to load shader from disk! %s", fullPath);
		return NULL;
	}

	SDL_GPUShaderCreateInfo shaderInfo = {
		.code = code,
		.code_size = codeSize,
		.entrypoint = entrypoint,
		.format = format,
		.stage = stage,
		.num_samplers = samplerCount,
		.num_uniform_buffers = uniformBufferCount,
		.num_storage_buffers = storageBufferCount,
		.num_storage_textures = storageTextureCount
	};
	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
	if (shader == NULL)
	{
		SDL_Log("Failed to create shader!");
		SDL_free(code);
		return NULL;
	}

	SDL_free(code);
	return shader;
}


int main(int arg, char** argv) {
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
	{
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		return 1;
	}

	SDL_GPUDevice* Device = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
		false,
		NULL);

	if (Device == NULL)
	{
		SDL_Log("GPUCreateDevice failed");
		return -1;
	}

	SDL_Window* Window = SDL_CreateWindow("SDL3 HDPI Bug", 640, 480, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
	if (Window == NULL)
	{
		SDL_Log("CreateWindow failed: %s", SDL_GetError());
		return -1;
	}

	if (!SDL_ClaimWindowForGPUDevice(Device, Window))
	{
		SDL_Log("GPUClaimWindow failed");
		return -1;
	}

	SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
	if (SDL_WindowSupportsGPUPresentMode(
		Device,
		Window,
		SDL_GPU_PRESENTMODE_IMMEDIATE
	)) {
		printf("Choosing SDL_GPU_PRESENTMODE_IMMEDIATE\n");
		presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
	}
	else if (SDL_WindowSupportsGPUPresentMode(
		Device,
		Window,
		SDL_GPU_PRESENTMODE_MAILBOX
	)) {
		printf("Choosing SDL_GPU_PRESENTMODE_MAILBOX\n");
		presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
	} else {
		printf("Choosing SDL_GPU_PRESENTMODE_VSYNC\n");
	}

	SDL_SetGPUSwapchainParameters(
		Device,
		Window,
		SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
		presentMode
	);


    SDL_GPUShader* vertexShader = LoadShader(Device, "RawTriangle.vert", 0, 0, 0, 0);
    if (vertexShader == NULL)
    {
        SDL_Log("Failed to create vertex shader!");
        return -1;
    }

    SDL_GPUShader* fragmentShader = LoadShader(Device, "SolidColor.frag", 0, 0, 0, 0);
    if (fragmentShader == NULL)
    {
        SDL_Log("Failed to create fragment shader!");
        return -1;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo = {
		.target_info = {
			.num_color_targets = 1,
			.color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
				.format = SDL_GetGPUSwapchainTextureFormat(Device, Window)
			}},
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.vertex_shader = vertexShader,
		.fragment_shader = fragmentShader,
        .rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL
	};

    SDL_GPUGraphicsPipeline* Pipeline = SDL_CreateGPUGraphicsPipeline(Device, &pipelineCreateInfo);
    if (Pipeline == NULL)
    {
        SDL_Log("Failed to create pipeline!");
        return -1;
    }

    SDL_ReleaseGPUShader(Device, vertexShader);
    SDL_ReleaseGPUShader(Device, fragmentShader);


	bool quit = false;
	while (!quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				quit = true;
			}

			SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(Device);
			if (cmdbuf == NULL)
			{
				SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
				return -1;
			}

			SDL_GPUTexture* swapchainTexture;
			if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, Window, &swapchainTexture, NULL, NULL)) {
				SDL_Log("WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
				return -1;
			}

			if (swapchainTexture != NULL)
			{
				SDL_GPUColorTargetInfo colorTargetInfo = { 0 };
				colorTargetInfo.texture = swapchainTexture;
				colorTargetInfo.clear_color = (SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };
				colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
				colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

				SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdbuf, &colorTargetInfo, 1, NULL);
				SDL_BindGPUGraphicsPipeline(renderPass, Pipeline);
				SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);
				SDL_EndGPURenderPass(renderPass);
			}

			SDL_SubmitGPUCommandBuffer(cmdbuf);
		}
	}

	return 0;
}
