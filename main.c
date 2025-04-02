#include <SDL3/SDL.h>

/* 
NOTE: This example code causes a macOS crash when you run it and maximize the window. 

Version tested: SDL3-3.2.10

It seems to occur when window's present mode is set to SDL_GPU_PRESENTMODE_IMMEDIATE, and the window is on my 120hz monitor. It does not occur on my 60hz monitor.

If you uncomment the #define FORCE_VSYNC_AND_PREVENT_CRASH line below, the crash will not happen.  
*/

// #define FORCE_VSYNC_AND_PREVENT_CRASH 1


static SDL_GPUGraphicsPipeline* RenderPipeline;
static SDL_GPUSampler* Sampler;
static SDL_GPUTexture* Texture;
static SDL_GPUTransferBuffer* SpriteDataTransferBuffer;
static SDL_GPUBuffer* SpriteDataBuffer;

typedef struct SpriteInstance
{
	float x, y, z;
	float rotation;
	float w, h, padding_a, padding_b;
	float tex_u, tex_v, tex_w, tex_h;
	float r, g, b, a;
} SpriteInstance;

// Matrix Math
typedef struct Matrix4x4
{
	float m11, m12, m13, m14;
	float m21, m22, m23, m24;
	float m31, m32, m33, m34;
	float m41, m42, m43, m44;
} Matrix4x4;

Matrix4x4 Matrix4x4_CreateOrthographicOffCenter(
	float left,
	float right,
	float bottom,
	float top,
	float zNearPlane,
	float zFarPlane
) {
	return (Matrix4x4) {
		2.0f / (right - left), 0, 0, 0,
		0, 2.0f / (top - bottom), 0, 0,
		0, 0, 1.0f / (zNearPlane - zFarPlane), 0,
		(left + right) / (left - right), (top + bottom) / (bottom - top), zNearPlane / (zNearPlane - zFarPlane), 1
	};
}

static const Uint32 SPRITE_COUNT = 8192;

static float uCoords[4] = { 0.0f, 0.5f, 0.0f, 0.5f };
static float vCoords[4] = { 0.0f, 0.0f, 0.5f, 0.5f };

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const char* shaderFilename, Uint32 samplerCount, Uint32 uniformBufferCount, Uint32 storageBufferCount, Uint32 storageTextureCount);

SDL_Surface* LoadImage(const char* imageFilename, int desiredChannels)
{
    const char* BasePath = SDL_GetBasePath();
	char fullPath[256];
	SDL_Surface *result;
	SDL_PixelFormat format;

	SDL_snprintf(fullPath, sizeof(fullPath), "%sContent/Images/%s", BasePath, imageFilename);

	result = SDL_LoadBMP(fullPath);
	if (result == NULL)
	{
		SDL_Log("Failed to load BMP: %s", SDL_GetError());
		return NULL;
	}

	if (desiredChannels == 4)
	{
		format = SDL_PIXELFORMAT_ABGR8888;
	}
	else
	{
		SDL_assert(!"Unexpected desiredChannels");
		SDL_DestroySurface(result);
		return NULL;
	}
	if (result->format != format)
	{
		SDL_Surface *next = SDL_ConvertSurface(result, format);
		SDL_DestroySurface(result);
		result = next;
	}

	return result;
}


int main(int arg, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    SDL_GPUDevice* Device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, NULL);

    if (Device == NULL) {
        SDL_Log("GPUCreateDevice failed");
        return -1;
    }

    SDL_Window* Window = SDL_CreateWindow("SDL3 HDPI Bug", 640, 480, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (Window == NULL) {
        SDL_Log("CreateWindow failed: %s", SDL_GetError());
        return -1;
    }

    if (!SDL_ClaimWindowForGPUDevice(Device, Window)) {
        SDL_Log("GPUClaimWindow failed");
        return -1;
    }

    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    #ifndef FORCE_VSYNC_AND_PREVENT_CRASH
    if (SDL_WindowSupportsGPUPresentMode(Device, Window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    } else if (SDL_WindowSupportsGPUPresentMode(Device, Window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
    } else {
        presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    }
    #endif

    switch (presentMode) {
        case SDL_GPU_PRESENTMODE_IMMEDIATE:
            printf("presentMode: SDL_GPU_PRESENTMODE_IMMEDIATE\n");
            break;
        case SDL_GPU_PRESENTMODE_MAILBOX:
            printf("presentMode: SDL_GPU_PRESENTMODE_MAILBOX\n");
            break;
        case SDL_GPU_PRESENTMODE_VSYNC:
            printf("presentMode: SDL_GPU_PRESENTMODE_VSYNC\n");
            break;
        default:
            printf("presentMode: UNKNOWN\n");
            break;
    }

    SDL_SetGPUSwapchainParameters(Device, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);

	SDL_srand(0);

	// Create the shaders
	SDL_GPUShader* vertShader = LoadShader(
		Device,
		"PullSpriteBatch.vert",
		0,
		1,
		1,
		0
	);

	SDL_GPUShader* fragShader = LoadShader(
		Device,
		"TexturedQuadColor.frag",
		1,
		0,
		0,
		0
	);

	// Create the sprite render pipeline
	RenderPipeline = SDL_CreateGPUGraphicsPipeline(
		Device,
		&(SDL_GPUGraphicsPipelineCreateInfo){
			.target_info = (SDL_GPUGraphicsPipelineTargetInfo){
				.num_color_targets = 1,
				.color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
					.format = SDL_GetGPUSwapchainTextureFormat(Device, Window),
					.blend_state = {
						.enable_blend = true,
						.color_blend_op = SDL_GPU_BLENDOP_ADD,
						.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
						.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
						.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
						.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
						.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
					}
				}}
			},
			.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
			.vertex_shader = vertShader,
			.fragment_shader = fragShader
		}
	);

	SDL_ReleaseGPUShader(Device, vertShader);
	SDL_ReleaseGPUShader(Device, fragShader);


	// Load the image data
	SDL_Surface *imageData = LoadImage("ravioli_atlas.bmp", 4);
	if (imageData == NULL)
	{
		SDL_Log("Could not load image data!");
		return -1;
	}

	SDL_GPUTransferBuffer* textureTransferBuffer = SDL_CreateGPUTransferBuffer(
		Device,
		&(SDL_GPUTransferBufferCreateInfo) {
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = imageData->w * imageData->h * 4
		}
	);

	Uint8 *textureTransferPtr = SDL_MapGPUTransferBuffer(
		Device,
		textureTransferBuffer,
		false
	);
	SDL_memcpy(textureTransferPtr, imageData->pixels, imageData->w * imageData->h * 4);
	SDL_UnmapGPUTransferBuffer(Device, textureTransferBuffer);

	// Create the GPU resources
	Texture = SDL_CreateGPUTexture(
		Device,
		&(SDL_GPUTextureCreateInfo){
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
			.width = imageData->w,
			.height = imageData->h,
			.layer_count_or_depth = 1,
			.num_levels = 1,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER
		}
	);

	Sampler = SDL_CreateGPUSampler(
		Device,
		&(SDL_GPUSamplerCreateInfo){
			.min_filter = SDL_GPU_FILTER_NEAREST,
			.mag_filter = SDL_GPU_FILTER_NEAREST,
			.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
			.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
			.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
		}
	);

	SpriteDataTransferBuffer = SDL_CreateGPUTransferBuffer(
		Device,
		&(SDL_GPUTransferBufferCreateInfo) {
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = SPRITE_COUNT * sizeof(SpriteInstance)
		}
	);

	SpriteDataBuffer = SDL_CreateGPUBuffer(
		Device,
		&(SDL_GPUBufferCreateInfo) {
			.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
			.size = SPRITE_COUNT * sizeof(SpriteInstance)
		}
	);

	// Transfer the up-front data
	SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(Device);
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);

	SDL_UploadToGPUTexture(
		copyPass,
		&(SDL_GPUTextureTransferInfo) {
			.transfer_buffer = textureTransferBuffer,
			.offset = 0, /* Zeroes out the rest */
		},
		&(SDL_GPUTextureRegion){
			.texture = Texture,
			.w = imageData->w,
			.h = imageData->h,
			.d = 1
		},
		false
	);

	SDL_EndGPUCopyPass(copyPass);
	SDL_SubmitGPUCommandBuffer(uploadCmdBuf);

    SDL_DestroySurface(imageData);
	SDL_ReleaseGPUTransferBuffer(Device, textureTransferBuffer);



    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }

        Matrix4x4 cameraMatrix = Matrix4x4_CreateOrthographicOffCenter(
            0,
            640,
            480,
            0,
            0,
            -1
        );

        SDL_GPUCommandBuffer* cmdbuf = SDL_AcquireGPUCommandBuffer(Device);
        if (cmdbuf == NULL) {
            SDL_Log("AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            return -1;
        }

        SDL_GPUTexture* swapchainTexture;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, Window, &swapchainTexture, NULL, NULL)) {
            SDL_Log("WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            return -1;
        }

        if (swapchainTexture != NULL) {
            // Build sprite instance transfer
            SpriteInstance* dataPtr = SDL_MapGPUTransferBuffer(
                Device,
                SpriteDataTransferBuffer,
                true
            );

            for (Uint32 i = 0; i < SPRITE_COUNT; i += 1)
            {
                Sint32 ravioli = SDL_rand(4);
                dataPtr[i].x = (float)(SDL_rand(640));
                dataPtr[i].y = (float)(SDL_rand(480));
                dataPtr[i].z = 0;
                dataPtr[i].rotation = SDL_randf() * SDL_PI_F * 2;
                dataPtr[i].w = 32;
                dataPtr[i].h = 32;
                dataPtr[i].tex_u = uCoords[ravioli];
                dataPtr[i].tex_v = vCoords[ravioli];
                dataPtr[i].tex_w = 0.5f;
                dataPtr[i].tex_h = 0.5f;
                dataPtr[i].r = 1.0f;
                dataPtr[i].g = 1.0f;
                dataPtr[i].b = 1.0f;
                dataPtr[i].a = 1.0f;
            }

            SDL_UnmapGPUTransferBuffer(Device, SpriteDataTransferBuffer);

            // Upload instance data
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdbuf);
            SDL_UploadToGPUBuffer(
                copyPass,
                &(SDL_GPUTransferBufferLocation) {
                    .transfer_buffer = SpriteDataTransferBuffer,
                    .offset = 0
                },
                &(SDL_GPUBufferRegion) {
                    .buffer = SpriteDataBuffer,
                    .offset = 0,
                    .size = SPRITE_COUNT * sizeof(SpriteInstance)
                },
                true
            );
            SDL_EndGPUCopyPass(copyPass);

            // Render sprites
            SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
                cmdbuf,
                &(SDL_GPUColorTargetInfo){
                    .texture = swapchainTexture,
                    .cycle = false,
                    .load_op = SDL_GPU_LOADOP_CLEAR,
                    .store_op = SDL_GPU_STOREOP_STORE,
                    .clear_color = { 0, 0, 0, 1 }
                },
                1,
                NULL
            );

            SDL_BindGPUGraphicsPipeline(renderPass, RenderPipeline);
            SDL_BindGPUVertexStorageBuffers(
                renderPass,
                0,
                &SpriteDataBuffer,
                1
            );
            SDL_BindGPUFragmentSamplers(
                renderPass,
                0,
                &(SDL_GPUTextureSamplerBinding){
                    .texture = Texture,
                    .sampler = Sampler
                },
                1
            );
            SDL_PushGPUVertexUniformData(
                cmdbuf,
                0,
                &cameraMatrix,
                sizeof(Matrix4x4)
            );
            SDL_DrawGPUPrimitives(
                renderPass,
                SPRITE_COUNT * 6,
                1,
                0,
                0
            );

            SDL_EndGPURenderPass(renderPass);
        }

        SDL_SubmitGPUCommandBuffer(cmdbuf);
    }

    SDL_ReleaseGPUGraphicsPipeline(Device, RenderPipeline);
    SDL_DestroyGPUDevice(Device);
    SDL_DestroyWindow(Window);
    SDL_Quit();

    return 0;
}

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const char* shaderFilename, Uint32 samplerCount, Uint32 uniformBufferCount, Uint32 storageBufferCount, Uint32 storageTextureCount) 
{
    const char* BasePath = SDL_GetBasePath();

    // Auto-detect the shader stage from the file name for convenience
    SDL_GPUShaderStage stage;
    if (SDL_strstr(shaderFilename, ".vert")) {
        stage = SDL_GPU_SHADERSTAGE_VERTEX;
    } else if (SDL_strstr(shaderFilename, ".frag")) {
        stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    } else {
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
    if (shader == NULL) {
        SDL_Log("Failed to create shader!");
        SDL_free(code);
        return NULL;
    }

    SDL_free(code);
    return shader;
}

