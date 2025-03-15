#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include <vector>
#include <iostream>

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

SDL_GPUBuffer* gVertexBuffer;
SDL_GPUBuffer* gIndicesBuffer;

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

SDL_GPUShader* loadSDLGPUShader(const char* filename, SDL_Storage* storage, SDL_GPUShaderStage stage) {
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
    ci.num_samplers = 0;
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
    const char* dir = "examples/02_buffer";
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
    bundle.vertex = loadSDLGPUShader("vert.spv", storage, SDL_GPU_SHADERSTAGE_VERTEX);
    bundle.fragment = loadSDLGPUShader("frag.spv", storage, SDL_GPU_SHADERSTAGE_FRAGMENT);

    SDL_CloseStorage(storage);
    return bundle;
}

SDL_GPUGraphicsPipeline* createGraphicsPipeline() {
    SDL_GPUGraphicsPipelineCreateInfo ci{};

    SDL_GPUVertexAttribute attributes[2];

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
    
    ci.vertex_input_state.vertex_attributes = attributes;
    ci.vertex_input_state.num_vertex_attributes = 2;

    SDL_GPUVertexBufferDescription buffer_desc;
    buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer_desc.instance_step_rate = 1;
    buffer_desc.slot = 0;
    buffer_desc.pitch = sizeof(float) * 5;
    
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
};

void createAndUploadVertexData() {
    Vertex vertices[] = {
        // left bottom
        Vertex{ -0.5, -0.5, 1.0, 0.0, 0.0},
        // right bottom
        Vertex{ 0.5,  -0.5, 0.0, 1.0, 0.0},
        // right top
        Vertex{0.5,  0.5, 0.0, 0.0, 1.0},
        // left top
        Vertex{ -0.5, 0.5, 1.0, 1.0, 0.0},
    };
    
    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = sizeof(vertices);
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gDevice, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gDevice, transfer_buffer, false);
    memcpy(ptr, &vertices, sizeof(vertices));
    SDL_UnmapGPUTransferBuffer(gDevice, transfer_buffer);

    SDL_GPUBufferCreateInfo gpu_buffer_ci;
    gpu_buffer_ci.size = sizeof(vertices);
    gpu_buffer_ci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

    gVertexBuffer = SDL_CreateGPUBuffer(gDevice, &gpu_buffer_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gDevice);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation location;
    location.offset = 0;
    location.transfer_buffer = transfer_buffer;
    SDL_GPUBufferRegion region;
    region.buffer = gVertexBuffer;
    region.offset = 0;
    region.size = sizeof(vertices);
    SDL_UploadToGPUBuffer(copy_pass, &location, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    
    SDL_ReleaseGPUTransferBuffer(gDevice, transfer_buffer);
}

void createAndUploadIndicesData() {
    uint32_t indices[] = {
        0, 1, 2,
        0, 3, 2,
    };
    
    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = sizeof(indices);
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(gDevice, &transfer_buffer_ci);
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
    binding.buffer = gVertexBuffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);

    SDL_GPUBufferBinding indices_binding;
    indices_binding.buffer = gIndicesBuffer;
    indices_binding.offset = 0;
    SDL_BindGPUIndexBuffer(render_pass, &indices_binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);

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

    SDL_ReleaseGPUBuffer(gDevice, gIndicesBuffer);
    SDL_ReleaseGPUBuffer(gDevice, gVertexBuffer);
    SDL_ReleaseGPUGraphicsPipeline(gDevice, gGraphicsPipeline);
    SDL_ReleaseGPUShader(gDevice, gShaders.vertex);
    SDL_ReleaseGPUShader(gDevice, gShaders.fragment);
    SDL_ReleaseWindowFromGPUDevice(gDevice, gWindow);
    SDL_DestroyGPUDevice(gDevice);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}