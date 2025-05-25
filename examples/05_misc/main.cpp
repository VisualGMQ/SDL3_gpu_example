#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "stb_image.h"
#include <iostream>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

struct GPUShaderBundle {
    SDL_GPUShader* vertex{};
    SDL_GPUShader* fragment{};

    operator bool() const { return vertex && fragment; }
};

SDL_Window* gWindow = nullptr;
bool gShouldExit = false;

struct GPUResources {
    SDL_GPUDevice* device = nullptr;
    GPUShaderBundle shaders;
    SDL_GPUGraphicsPipeline* graphicsPipeline{};

    SDL_GPUBuffer* planeVertexBuffer{};

    SDL_GPUTexture* transparentTexture{};
    SDL_GPUTexture* floorTexture{};
    SDL_GPUTexture* depthTexture{};
    SDL_GPUSampler* sampler{};

    void Destroy() {
        SDL_ReleaseGPUSampler(device, sampler);
        SDL_ReleaseGPUTexture(device, transparentTexture);
        SDL_ReleaseGPUTexture(device, floorTexture);
        SDL_ReleaseGPUTexture(device, depthTexture);
        SDL_ReleaseGPUBuffer(device, planeVertexBuffer);
        SDL_ReleaseGPUGraphicsPipeline(device, graphicsPipeline);
        SDL_ReleaseGPUShader(device, shaders.vertex);
        SDL_ReleaseGPUShader(device, shaders.fragment);
        SDL_ReleaseWindowFromGPUDevice(device, gWindow);
        SDL_DestroyGPUDevice(device);
    }
} gGPUResources;

struct Plane {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale = glm::vec3(1, 1, 1);
    glm::vec4 color;
    SDL_GPUTexture* texture{};
};

std::vector<Plane> gPlanes;

struct FlyCamera {
    void MoveTo(const glm::vec3& p) {
        position = p;
    }

    void Move(const glm::vec3& offset) {
        position += offset;
    }

    void RotateX(float angle) {
        rotation.x += angle;
        rotation.x = glm::clamp(rotation.x, -89.0f, 89.0f);
    }

    void RotateY(float angle) {
        rotation.y += angle;
    }
    
    const glm::mat4& GetMat() const {
        return mat;
    }

    void Update() {
        mat =
            glm::translate(
            glm::rotate(
            glm::rotate(
                glm::rotate(glm::mat4(1.0), -glm::radians(rotation.x), glm::vec3(1, 0, 0)),
                -glm::radians(rotation.y), glm::vec3(0, 1, 0)),
                -glm::radians(rotation.z), glm::vec3(0, 0, 1)),
                -position);
    }

private:
    glm::mat4 mat = glm::mat4(1.0);
    glm::vec3 position = glm::vec3(0, 0, 0);
    glm::vec3 rotation = glm::vec3(0, 0, 0);
} gCamera;

struct MVP {
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 model;
} gMVP;

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 720

bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "SDL init failed");
        return false;
    }

    for (int i = 0; i < SDL_GetNumGPUDrivers(); i++) {
        std::cout << SDL_GetGPUDriver(i) << std::endl;
    }

    // NOTE: must create gpu gGPUResources.device firstly, then create gWindow
    gGPUResources.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                                   SDL_GPU_SHADERFORMAT_DXIL |
                                                   SDL_GPU_SHADERFORMAT_MSL,
                                               true, nullptr);
    if (!gGPUResources.device) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU,
                     "SDL create gpu gGPUResources.device failed: %s",
                     SDL_GetError());
        return false;
    }

    gWindow = SDL_CreateWindow("cube", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!gWindow) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "SDL create gWindow failed");
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(gGPUResources.device, gWindow)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Your system don't support SDL GPU");
        return false;
    }

    return true;
}

SDL_GPUShader* loadSDLGPUShader(const char* filename, SDL_Storage* storage,
                                SDL_GPUShaderStage stage, uint32_t sampler_num,
                                uint32_t uniform_buffer_num) {
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
    ci.num_uniform_buffers = uniform_buffer_num;
    ci.num_storage_buffers = 0;
    ci.num_storage_textures = 0;
    ci.stage = stage;

    SDL_GPUShader* shader = SDL_CreateGPUShader(gGPUResources.device, &ci);
    if (!shader) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU,
                     "create gpu shader from %s failed: %s", filename,
                     SDL_GetError());
        return {};
    }
    return shader;
}

GPUShaderBundle createSDLGPUShaderBundle() {
    const char* dir = "examples/05_misc";
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
        loadSDLGPUShader("vert.spv", storage, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    bundle.fragment = loadSDLGPUShader("frag.spv", storage,
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

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
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = 0;
    }

    // uv attribute
    {
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[1].offset = sizeof(float) * 3;
    }

    ci.vertex_input_state.vertex_attributes = attributes;
    ci.vertex_input_state.num_vertex_attributes = std::size(attributes);

    SDL_GPUVertexBufferDescription buffer_desc;
    buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer_desc.instance_step_rate = 1;
    buffer_desc.slot = 0;
    buffer_desc.pitch = sizeof(float) * 5;

    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_buffer_descriptions = &buffer_desc;

    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.vertex_shader = gGPUResources.shaders.vertex;
    ci.fragment_shader = gGPUResources.shaders.fragment;

    ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

    ci.multisample_state.enable_mask = false;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    ci.target_info.num_color_targets = 1;
    ci.target_info.has_depth_stencil_target = true;
    ci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

    // depth stencil state
    SDL_GPUDepthStencilState state{};
    state.back_stencil_state.compare_op = SDL_GPU_COMPAREOP_NEVER;
    state.back_stencil_state.pass_op = SDL_GPU_STENCILOP_ZERO;
    state.back_stencil_state.fail_op = SDL_GPU_STENCILOP_ZERO;
    state.back_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_ZERO;
    state.compare_op = SDL_GPU_COMPAREOP_LESS;
    state.enable_depth_test = true;
    state.enable_depth_write = true;
    state.enable_stencil_test = false;
    state.compare_mask = 0xFF;
    state.write_mask = 0xFF;
    ci.depth_stencil_state = state;

    SDL_GPUColorTargetDescription desc;
    desc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    desc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    desc.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_A | SDL_GPU_COLORCOMPONENT_R |
        SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
    desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.blend_state.enable_blend = true;
    desc.blend_state.enable_color_write_mask = false;
    desc.format =
        SDL_GetGPUSwapchainTextureFormat(gGPUResources.device, gWindow);

    ci.target_info.color_target_descriptions = &desc;

    return SDL_CreateGPUGraphicsPipeline(gGPUResources.device, &ci);
}

struct Vertex {
    float x, y, z;
    float u, v;
};

void createAndUploadVertexData() {
    // clang-format off
    Vertex vertices[] = {
        Vertex{-0.5f, -0.5f, 0.0f,  0.0f, 0.0f},
        Vertex{ 0.5f, -0.5f, 0.0f,  1.0f, 0.0f},
        Vertex{ 0.5f,  0.5f, 0.0f,  1.0f, 1.0f},
        Vertex{ 0.5f,  0.5f, 0.0f,  1.0f, 1.0f},
        Vertex{-0.5f,  0.5f, 0.0f,  0.0f, 1.0f},
        Vertex{-0.5f, -0.5f, 0.0f,  0.0f, 0.0f},
    };
    //clang-format on

    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = sizeof(vertices);
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(gGPUResources.device, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gGPUResources.device, transfer_buffer, false);
    memcpy(ptr, &vertices, sizeof(vertices));
    SDL_UnmapGPUTransferBuffer(gGPUResources.device, transfer_buffer);

    SDL_GPUBufferCreateInfo gpu_buffer_ci;
    gpu_buffer_ci.size = sizeof(vertices);
    gpu_buffer_ci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

    gGPUResources.planeVertexBuffer = SDL_CreateGPUBuffer(gGPUResources.device, &gpu_buffer_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gGPUResources.device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation location;
    location.offset = 0;
    location.transfer_buffer = transfer_buffer;
    SDL_GPUBufferRegion region;
    region.buffer = gGPUResources.planeVertexBuffer;
    region.offset = 0;
    region.size = sizeof(vertices);
    SDL_UploadToGPUBuffer(copy_pass, &location, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gGPUResources.device, transfer_buffer);
}

SDL_GPUTexture* createImageTexture(const char* filename) {
    int w, h;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data =
        stbi_load(filename, &w, &h, NULL, STBI_rgb_alpha);

    size_t image_size = 4 * w * h;
    SDL_GPUTransferBufferCreateInfo transfer_buffer_ci;
    transfer_buffer_ci.size = image_size;
    transfer_buffer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

    SDL_GPUTransferBuffer* transfer_buffer =
        SDL_CreateGPUTransferBuffer(gGPUResources.device, &transfer_buffer_ci);
    void* ptr = SDL_MapGPUTransferBuffer(gGPUResources.device, transfer_buffer, false);
    memcpy(ptr, data, image_size);
    SDL_UnmapGPUTransferBuffer(gGPUResources.device, transfer_buffer);

    SDL_GPUTextureCreateInfo texture_ci;
    texture_ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_ci.height = h;
    texture_ci.width = w;
    texture_ci.layer_count_or_depth = 1;
    texture_ci.num_levels = 1;
    texture_ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ci.type = SDL_GPU_TEXTURETYPE_2D;
    texture_ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(gGPUResources.device, &texture_ci);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gGPUResources.device);
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
    region.texture = texture;

    SDL_UploadToGPUTexture(copy_pass, &transfer_info, &region, false);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gGPUResources.device, transfer_buffer);

    stbi_image_free(data);

    return texture;
}

void createDepthTexture(int w, int h) {
    SDL_GPUTextureCreateInfo texture_ci;
    texture_ci.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    texture_ci.height = h;
    texture_ci.width = w;
    texture_ci.layer_count_or_depth = 1;
    texture_ci.num_levels = 1;
    texture_ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ci.type = SDL_GPU_TEXTURETYPE_2D;
    texture_ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

    gGPUResources.depthTexture = SDL_CreateGPUTexture(gGPUResources.device, &texture_ci);
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

    gGPUResources.sampler = SDL_CreateGPUSampler(gGPUResources.device, &ci);
}

void initMVPData() {
    gMVP.proj = glm::perspective(glm::radians(45.0f), (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.01f, 1000.0f);
    gMVP.model = glm::mat4(1.0);
    gMVP.view = glm::mat4(1.0);
}

void initPlanes() {
    // floor
    {
        Plane plane;
        plane.color = glm::vec4(1, 1, 1, 1);
        plane.position = glm::vec3(0, 0, -0.5);
        plane.rotation = glm::vec3(-90, 0, 0);
        plane.scale = glm::vec3(10, 10, 10);
        plane.texture = gGPUResources.floorTexture;

        gPlanes.push_back(plane);
    }
    
    {
        Plane plane;
        plane.color = glm::vec4(0.5, 0, 0, 1);
        plane.position = glm::vec3(0.2, 0, -4);
        plane.rotation = glm::vec3(0, 0, 0);
        plane.texture = gGPUResources.transparentTexture;

        gPlanes.push_back(plane);
    }

    {
        Plane plane;
        plane.color = glm::vec4(0.5, 0, 0, 1);
        plane.position = glm::vec3(-0.2, 0, -3);
        plane.rotation = glm::vec3(0, 0, 0);
        plane.texture = gGPUResources.transparentTexture;

        gPlanes.push_back(plane);
    }
}

// SDL main loop

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    if (!initSDL()) {
        return SDL_APP_FAILURE;
    }

    gGPUResources.shaders = createSDLGPUShaderBundle();
    if (!gGPUResources.shaders) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Shader load failed! Program exit!");
        return SDL_APP_FAILURE;
    }

    gGPUResources.graphicsPipeline = createGraphicsPipeline();
    if (!gGPUResources.graphicsPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Graphics pipeline load failed! %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetWindowRelativeMouseMode(gWindow, true);

    createAndUploadVertexData();
    gGPUResources.transparentTexture = createImageTexture("examples/05_misc/assets/blending_transparent_window.png");
    gGPUResources.floorTexture = createImageTexture("examples/05_misc/assets/floor.png");
    createDepthTexture(WINDOW_WIDTH, WINDOW_HEIGHT);
    createSampler();
    initMVPData();

    initPlanes();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    gCamera.Update();
    gMVP.view = gCamera.GetMat();
    
    bool is_minimized = SDL_GetWindowFlags(gWindow) & SDL_WINDOW_MINIMIZED;
    if (is_minimized) {
        return SDL_APP_CONTINUE;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gGPUResources.device);
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

    SDL_GPUDepthStencilTargetInfo depth_target_info{};
    depth_target_info.clear_depth = 1;
    depth_target_info.cycle = false;
    depth_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target_info.texture = gGPUResources.depthTexture;
    
    SDL_GPURenderPass* render_pass =
        SDL_BeginGPURenderPass(cmd, &color_target_info, 1, &depth_target_info);
    SDL_BindGPUGraphicsPipeline(render_pass, gGPUResources.graphicsPipeline);

    SDL_GPUBufferBinding binding;
    binding.buffer = gGPUResources.planeVertexBuffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);

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

    for (auto& plane : gPlanes) {
        MVP mvp = gMVP;
        
        mvp.model =
            glm::scale(
            glm::translate(
                        glm::rotate(
                            glm::rotate(
                                glm::rotate(glm::mat4(1.0),
                                    glm::radians(plane.rotation.x), glm::vec3(1, 0, 0)),
                                glm::radians(plane.rotation.y), glm::vec3(0, 1, 0)),
                            glm::radians(plane.rotation.z), glm::vec3(0, 0, 1)),
                        plane.position), plane.scale);

        SDL_GPUTextureSamplerBinding sampler_binding;
        sampler_binding.texture = plane.texture;
        sampler_binding.sampler = gGPUResources.sampler;
        SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
        SDL_PushGPUVertexUniformData(cmd, 0, &mvp, sizeof(mvp));
        SDL_PushGPUFragmentUniformData(cmd, 0, &plane.color, sizeof(plane.color));
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }

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

    const float speed = 0.1;
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_A) {
            gCamera.Move(glm::vec3(-speed, 0, 0));
        }
        if (event->key.key == SDLK_D) {
            gCamera.Move(glm::vec3(speed, 0, 0));
        }
        if (event->key.key == SDLK_W) {
            gCamera.Move(glm::vec3(0, 0, -speed));
        }
        if (event->key.key == SDLK_S) {
            gCamera.Move(glm::vec3(0, 0, speed));
        }
        if (event->key.key == SDLK_ESCAPE) {
            return SDL_APP_SUCCESS;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const float factor = 0.1;
        gCamera.RotateX(-event->motion.yrel * factor);
        gCamera.RotateY(-event->motion.xrel * factor);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    SDL_WaitForGPUIdle(gGPUResources.device);

    gGPUResources.Destroy();
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}