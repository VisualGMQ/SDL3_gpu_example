#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "stb_image.h"
#include <iostream>
#include <vector>

struct GPUShaderBundle {
    SDL_GPUShader* vertex{};
    SDL_GPUShader* fragment{};

    operator bool() const { return vertex && fragment; }
};

SDL_Window* gWindow = nullptr;
bool gShouldExit = false;

// SDL GPU resources
SDL_GPUDevice* gDevice = nullptr;
GPUShaderBundle gShaders;
SDL_GPUGraphicsPipeline* gGraphicsPipeline;

SDL_GPUBuffer* gPlaneVertexBuffer;
SDL_GPUBuffer* gIndicesBuffer;

SDL_GPUTexture* gTexture;
SDL_GPUSampler* gSampler;

bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "SDL init failed");
        return false;
    }

    for (int i = 0; i < SDL_GetNumGPUDrivers(); i++) {
        std::cout << SDL_GetGPUDriver(i) << std::endl;
    }

    // NOTE: must create gpu gDevice firstly, then create gWindow
    gDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                      SDL_GPU_SHADERFORMAT_DXIL |
                                      SDL_GPU_SHADERFORMAT_MSL,
                                  true, nullptr);
    if (!gDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL create gpu gDevice failed: %s",
                     SDL_GetError());
        return false;
    }

    gWindow = SDL_CreateWindow("triangle", 1024, 720, SDL_WINDOW_RESIZABLE);
    if (!gWindow) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "SDL create gWindow failed");
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(gDevice, gWindow)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Your system don't support SDL GPU");
        return false;
    }

    return true;
}

SDL_GPUShader* loadSDLGPUShader(const char* filename, SDL_Storage* storage,
                                SDL_GPUShaderStage stage, uint32_t sampler_num) {
    Uint64 file_size;
    if (!SDL_GetStorageFileSize(storage, filename, &file_size)) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "get file %s size failed!: %s",
                     filename, SDL_GetError());
        return {};
    }

    std::vector<Uint8> data(file_size);
    if (!SDL_ReadStorageFile(storage, filename, data.data(), file_size)) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "read file %s failed!: %s",
                     filename, SDL_GetError());
        return {};
    }

    SDL_GPUShaderCreateInfo ci;
    ci.code = data.data();
    ci.code_size = data.size();
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.num_samplers = sampler_num;
    ci.num_storage_buffers = 0;
    ci.num_storage_textures = 0;
    ci.num_uniform_buffers = 0;
    ci.stage = stage;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gDevice, &ci);
    if (!shader) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU,
                     "create gpu shader from %s failed: %s", filename,
                     SDL_GetError());
        return {};
    }
    return shader;
}

GPUShaderBundle createSDLGPUShaderBundle() {
    const char* dir = "examples/03_texture";
    SDL_Storage* storage = SDL_OpenFileStorage(dir);
    if (!storage) {
        SDL_LogError(
            SDL_LOG_CATEGORY_SYSTEM,
            "Open storage %s failed! You must run this program at root dir!",
            dir);
        return {};
    }

    while (!SDL_StorageReady(storage)) {
        SDL_Delay(1);
    }

    GPUShaderBundle bundle;
    bundle.vertex =
        loadSDLGPUShader("vert.spv", storage, SDL_GPU_SHADERSTAGE_VERTEX, 0);
    bundle.fragment =
        loadSDLGPUShader("frag.spv", storage, SDL_GPU_SHADERSTAGE_FRAGMENT, 1);

    SDL_CloseStorage(storage);
    return bundle;
}

SDL_GPUGraphicsPipeline* createGraphicsPipeline() {
    SDL_GPUGraphicsPipelineCreateInfo ci{};

    SDL_GPUVertexAttribute attributes[3];

    // position attribute
    {
        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[0].offset = 0;
    }

    // color attribute
    {
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = sizeof(float) * 2;
    }

    // uv attribute
    {
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[2].offset = sizeof(float) * 5;
    }

    ci.vertex_input_state.vertex_attributes = attributes;
    ci.vertex_input_state.num_vertex_attributes = 3;

    SDL_GPUVertexBufferDescription buffer_desc;
    buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer_desc.instance_step_rate = 1;
    buffer_desc.slot = 0;
    buffer_desc.pitch = sizeof(float) * 7;

    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_buffer_descriptions = &buffer_desc;

    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.vertex_shader = gShaders.vertex;
    ci.fragment_shader = gShaders.fragment;

    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

    ci.multisample_state.enable_mask = false;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    ci.target_info.num_color_targets = 1;
    ci.target_info.has_depth_stencil_target = false;

    SDL_GPUColorTargetDescription desc;
    desc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    desc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    desc.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_A | SDL_GPU_COLORCOMPONENT_R |
        SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
    desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    desc.blend_state.enable_blend = true;
    desc.blend_state.enable_color_write_mask = false;
    desc.format = SDL_GetGPUSwapchainTextureFormat(gDevice, gWindow);

    ci.target_info.color_target_descriptions = &desc;

    return SDL_CreateGPUGraphicsPipeline(gDevice, &ci);
}

struct Vertex {
    float x, y;
    float r, g, b;
    float u, v;
};

void createAndUploadVertexData() {
    Vertex vertices[] = {
        // left bottom
        Vertex{-0.5, -0.5, 1.0, 0.0, 0.0, 0, 0},
        // right bottom
        Vertex{ 0.5, -0.5, 0.0, 1.0, 0.0, 1, 0},
        // right top
        Vertex{ 0.5,  0.5, 0.0, 0.0, 1.0, 1, 1},
        // left top
        Vertex{-0.5,  0.5, 1.0, 1.0, 0.0, 0, 1},
    };

    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = sizeof(vertices);
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(gDevice, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gDevice, transfer_buffer, false);
    memcpy(ptr, &vertices, sizeof(vertices));
    SDL_UnmapGPUTransferBuffer(gDevice, transfer_buffer);

    SDL_GPUBufferCreateInfo gpu_buffer_ci;
    gpu_buffer_ci.size = sizeof(vertices);
    gpu_buffer_ci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

    gPlaneVertexBuffer = SDL_CreateGPUBuffer(gDevice, &gpu_buffer_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation location;
    location.offset = 0;
    location.transfer_buffer = transfer_buffer;
    SDL_GPUBufferRegion region;
    region.buffer = gPlaneVertexBuffer;
    region.offset = 0;
    region.size = sizeof(vertices);
    SDL_UploadToGPUBuffer(copy_pass, &location, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gDevice, transfer_buffer);
}

void createAndUploadIndicesData() {
    uint32_t indices[] = {
        0, 1, 2, 0, 3, 2,
    };

    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = sizeof(indices);
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(gDevice, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gDevice, transfer_buffer, false);
    memcpy(ptr, &indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(gDevice, transfer_buffer);

    SDL_GPUBufferCreateInfo gpu_buffer_ci;
    gpu_buffer_ci.size = sizeof(indices);
    gpu_buffer_ci.usage = SDL_GPU_BUFFERUSAGE_INDEX;

    gIndicesBuffer = SDL_CreateGPUBuffer(gDevice, &gpu_buffer_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation location;
    location.offset = 0;
    location.transfer_buffer = transfer_buffer;
    SDL_GPUBufferRegion region;
    region.buffer = gIndicesBuffer;
    region.offset = 0;
    region.size = sizeof(indices);
    SDL_UploadToGPUBuffer(copy_pass, &location, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gDevice, transfer_buffer);
}

void createImageTexture() {
    int w, h;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data =
        stbi_load("examples/03_texture/girl.png", &w, &h, NULL, STBI_rgb_alpha);

    size_t image_size = 4 * w * h;
    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = image_size;
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(gDevice, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gDevice, transfer_buffer, false);
    memcpy(ptr, data, image_size);
    SDL_UnmapGPUTransferBuffer(gDevice, transfer_buffer);

    SDL_GPUTextureCreateInfo texture_ci;
    texture_ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_ci.height = h;
    texture_ci.width = w;
    texture_ci.layer_count_or_depth = 1;
    texture_ci.num_levels = 1;
    texture_ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ci.type = SDL_GPU_TEXTURETYPE_2D;
    texture_ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    gTexture = SDL_CreateGPUTexture(gDevice, &texture_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo transfer_info;
    transfer_info.offset = 0;
    transfer_info.pixels_per_row = w;
    transfer_info.rows_per_layer = h;
    transfer_info.transfer_buffer = transfer_buffer;

    SDL_GPUTextureRegion region;
    region.w = w;
    region.h = h;
    region.x = 0;
    region.y = 0;
    region.layer = 0;
    region.mip_level = 0;
    region.z = 0;
    region.d = 1;
    region.texture = gTexture;

    SDL_UploadToGPUTexture(copy_pass, &transfer_info, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gDevice, transfer_buffer);

    stbi_image_free(data);
}

void createSampler() {
    SDL_GPUSamplerCreateInfo ci;
    ci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.enable_anisotropy = false;
    ci.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    ci.enable_compare = false;
    ci.mag_filter = SDL_GPU_FILTER_LINEAR;
    ci.min_filter = SDL_GPU_FILTER_LINEAR;
    ci.max_lod = 1.0;
    ci.min_lod = 1.0;
    ci.mip_lod_bias = 0.0;
    ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;

    gSampler = SDL_CreateGPUSampler(gDevice, &ci);
}

// SDL main loop

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    if (!initSDL()) {
        return SDL_APP_FAILURE;
    }

    gShaders = createSDLGPUShaderBundle();
    if (!gShaders) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Shader load failed! Program exit!");
        return SDL_APP_FAILURE;
    }

    gGraphicsPipeline = createGraphicsPipeline();
    if (!gGraphicsPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Graphics pipeline load failed! %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }

    createAndUploadVertexData();
    createAndUploadIndicesData();
    createImageTexture();
    createSampler();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    bool is_minimized = SDL_GetWindowFlags(gWindow) & SDL_WINDOW_MINIMIZED;
    if (is_minimized) {
        return SDL_APP_CONTINUE;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 width, height;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, gWindow, &swapchain_texture,
                                               &width, &height)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU,
                     "SDL swapchain texture acquire failed! %s",
                     SDL_GetError());
    }

    if (!swapchain_texture) {
        return SDL_APP_CONTINUE;
    }

    SDL_GPUColorTargetInfo color_target_info{};
    color_target_info.clear_color.r = 0.1;
    color_target_info.clear_color.g = 0.1;
    color_target_info.clear_color.b = 0.1;
    color_target_info.clear_color.a = 1;
    color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target_info.mip_level = 0;
    color_target_info.store_op = SDL_GPU_STOREOP_STORE;
    color_target_info.texture = swapchain_texture;
    color_target_info.cycle = true;
    color_target_info.layer_or_depth_plane = 0;
    color_target_info.cycle_resolve_texture = false;
    SDL_GPURenderPass* render_pass =
        SDL_BeginGPURenderPass(cmd, &color_target_info, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(render_pass, gGraphicsPipeline);

    SDL_GPUBufferBinding binding;
    binding.buffer = gPlaneVertexBuffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);

    SDL_GPUBufferBinding indices_binding;
    indices_binding.buffer = gIndicesBuffer;
    indices_binding.offset = 0;
    SDL_BindGPUIndexBuffer(render_pass, &indices_binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_GPUTextureSamplerBinding sampler_binding;
    sampler_binding.texture = gTexture;
    sampler_binding.sampler = gSampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

    int window_width, window_height;
    SDL_GetWindowSize(gWindow, &window_width, &window_height);

    SDL_GPUViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = window_width;
    viewport.h = window_height;
    viewport.min_depth = 0;
    viewport.max_depth = 1;
    SDL_SetGPUViewport(render_pass, &viewport);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);

    SDL_EndGPURenderPass(render_pass);

    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU,
                     "SDL submit command buffer failed! %s", SDL_GetError());
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    SDL_WaitForGPUIdle(gDevice);

    SDL_ReleaseGPUSampler(gDevice, gSampler);
    SDL_ReleaseGPUTexture(gDevice, gTexture);
    SDL_ReleaseGPUBuffer(gDevice, gIndicesBuffer);
    SDL_ReleaseGPUBuffer(gDevice, gPlaneVertexBuffer);
    SDL_ReleaseGPUGraphicsPipeline(gDevice, gGraphicsPipeline);
    SDL_ReleaseGPUShader(gDevice, gShaders.vertex);
    SDL_ReleaseGPUShader(gDevice, gShaders.fragment);
    SDL_ReleaseWindowFromGPUDevice(gDevice, gWindow);
    SDL_DestroyGPUDevice(gDevice);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}