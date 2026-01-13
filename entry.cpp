#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_log.h"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_scancode.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"

#include <expected>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

enum class Quadrant {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

Quadrant GetClickQuadrant(SDL_Window* window, int mouseX, int mouseY) {
    int w, h;
    SDL_GetWindowSize(window, &w, &h); // get current window size

    bool left = mouseX < w / 2;
    bool top  = mouseY < h / 2;

    if (top && left) return Quadrant::TopLeft;
    if (top && !left) return Quadrant::TopRight;
    if (!top && left) return Quadrant::BottomLeft;
    return Quadrant::BottomRight;
}

void PlaySound(SDL_AudioDeviceID audioDeviceID, SDL_AudioSpec* deviceAudioSpec, SDL_AudioStream** audioStream, std::string wavFilename) {
    std::string pathRoot{SDL_GetBasePath()};
    std::string fullPath{std::format("{}audio/{}.wav", pathRoot, wavFilename)};

    SDL_AudioSpec spec{};
    Uint8* buffer;
    Uint32 bufferLength;
    if (!SDL_LoadWAV(fullPath.c_str(), &spec, &buffer, &bufferLength)) {
        SDL_Log("Failed to load audio file from disk: %s", SDL_GetError());
        return;
    }
    std::vector<Uint8> audioBuffer{buffer, buffer+bufferLength};
    SDL_free(buffer);

    *audioStream = SDL_CreateAudioStream(&spec, deviceAudioSpec);
    if (!*audioStream) {
        SDL_Log("Failed to create audio stream");
        return;
    }

    if (!SDL_BindAudioStream(audioDeviceID, *audioStream)) {
        SDL_Log("Failed to bind audio stream to device: %s", SDL_GetError());
        SDL_DestroyAudioStream(*audioStream);
        return;
    }

    if (!SDL_PutAudioStreamData(*audioStream, audioBuffer.data(), audioBuffer.size())) {
        SDL_Log("Failed to push audio data to stream: %s", SDL_GetError());
        SDL_UnbindAudioStream(*audioStream);
        SDL_DestroyAudioStream(*audioStream);
        return;
    }
    //SDL_ResumeAudioDevice(audioDeviceID);
    if (!SDL_ResumeAudioStreamDevice(*audioStream)) {
        SDL_Log("Failed to resume playback: %s", SDL_GetError());
        SDL_UnbindAudioStream(*audioStream);
        SDL_DestroyAudioStream(*audioStream);
        return;
    };
    if (!SDL_FlushAudioStream(*audioStream)) {
        SDL_Log("Failed to flush audio stream: %s", SDL_GetError());
        SDL_UnbindAudioStream(*audioStream);
        SDL_DestroyAudioStream(*audioStream);
        return;
    }
}

typedef struct PositionTextureVertex {
    float x, y, z;
    float u, v;
} PositionTextureVertex;

void play(SDL_AudioStream* audioStream, std::string &sfxName) {

}

SDL_GPUShader* CreateShader(
    SDL_GPUDevice* gpu,
    std::string &shaderFilename,
    Uint32 samplerCount,
    Uint32 uniformBufferCount,
    Uint32 storageBufferCount,
    Uint32 storageTextureCount
) {
    SDL_GPUShaderStage stage;
    if(shaderFilename.contains(".vert")) {
        stage = SDL_GPU_SHADERSTAGE_VERTEX;
    } else if (shaderFilename.contains(".frag")) {
        stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    } else {
        SDL_Log("Incorrect shader stage");
        return nullptr;
    }

    std::string fullPath;
    SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(gpu);
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* base = SDL_GetBasePath(); // TODO: I dunno, is this a leak?
    std::string pathRoot = base;
    
    const char *entrypoint;

    if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
        fullPath = std::format("{}shaders/compiled/SPIRV/{}.spv", pathRoot, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        entrypoint = "main";
    } else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
        fullPath = std::format("{}shaders/compiled/MSL/{}.msl", pathRoot, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_MSL;
        entrypoint = "main0";
    } else if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
        fullPath = std::format("{}shaders/compiled/DXIL/{}.dxil", pathRoot, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_DXIL;
        entrypoint = "main";
    } else {
        SDL_Log("%s", "Unknown backend shader format");
        return nullptr;
    }

    std::ifstream file{fullPath, std::ios::binary};
    if (!file) {
        SDL_Log("Failed to read shader from disk: %s", fullPath.c_str());
        return nullptr;
    }
    std::vector<Uint8> code{std::istreambuf_iterator(file), {}};

    SDL_GPUShaderCreateInfo shaderInfo{};
    shaderInfo.code = code.data();
    shaderInfo.code_size = code.size();
    shaderInfo.entrypoint = entrypoint;
    shaderInfo.format = format;
    shaderInfo.num_samplers = samplerCount;
    shaderInfo.num_storage_buffers = storageBufferCount;
    shaderInfo.num_uniform_buffers = uniformBufferCount;
    shaderInfo.num_storage_textures = storageTextureCount;
    shaderInfo.stage = stage;

    SDL_GPUShader *shader = SDL_CreateGPUShader(gpu, &shaderInfo);
    if (shader == nullptr) {
        SDL_Log("Failed to create shader: %s", shaderFilename.c_str());
        return nullptr;
    }
    
    return shader;
}

SDL_Surface* LoadImage(std::string &imageFilename, int desiredChannels) {
    const char* base = SDL_GetBasePath(); // TODO: I dunno, is this a leak?
    std::string pathRoot = base;
    std::string fullPath{std::format("{}image/{}", pathRoot.c_str(), imageFilename.c_str())};
    SDL_Surface* result{SDL_LoadBMP(fullPath.c_str())};
    if (!result) {
        SDL_Log("Failed to read image from disk: %s, %s", fullPath.c_str(), SDL_GetError());
        return nullptr;
    }
    if (desiredChannels != 4) {
        SDL_Log("invalid desired channels for: %s", fullPath.c_str());
        SDL_DestroySurface(result);
    }
    SDL_PixelFormat format{SDL_PIXELFORMAT_ABGR8888};

    if (result->format != format) {
        SDL_Surface* next = SDL_ConvertSurface(result, format);
        SDL_DestroySurface(result);
        result = next;
    }
    return result;
}

class AppData {
    private:
        AppData(SDL_Window *window, std::vector<std::string> sounds, SDL_AudioDeviceID audioDevice, SDL_AudioSpec audioDeviceSpec, SDL_GPUDevice *gpu, SDL_GPUGraphicsPipeline *pipelineGraphics, SDL_GPUBuffer *vertexBuffer, SDL_GPUBuffer *indexBuffer, SDL_GPUTexture *texture, SDL_GPUSampler *sampler) 
        : window(window), sounds(sounds), audioDevice(audioDevice), audioDeviceSpec(audioDeviceSpec), audioStream(nullptr), gpu(gpu), pipelineGraphics(pipelineGraphics), vertexBuffer(vertexBuffer), indexBuffer(indexBuffer), texture(texture), sampler(sampler) {};
    public:
        SDL_Window *window;
        std::vector<std::string> sounds;
        SDL_AudioDeviceID audioDevice;
        SDL_AudioSpec audioDeviceSpec;
        SDL_AudioStream *audioStream;
        SDL_GPUDevice *gpu;
        SDL_GPUGraphicsPipeline *pipelineGraphics;
        SDL_GPUBuffer *vertexBuffer;
        SDL_GPUBuffer *indexBuffer;
        SDL_GPUTexture *texture;
        SDL_GPUSampler *sampler;

        // bezpieczniejszy konstruktor
        static std::expected<std::unique_ptr<AppData>, std::string> New_AppData() {
            // Window
            float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
            SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
            SDL_Window *window  = SDL_CreateWindow("Fuel Fraction Calculator", static_cast<int>(800 * main_scale), static_cast<int>(600*main_scale), window_flags);
            if (!window) {
                return std::unexpected(std::format("Couldn't create window: {}", SDL_GetError()));
            }
            SDL_Log("Created window");
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            
            // Audio
            std::vector<std::string> sounds{"Factorio_research", "GMod_ragdoll", "Isaac_enter", "Minecraft_eating"};
            SDL_AudioSpec desiredAudioSpec{};
            desiredAudioSpec.format = SDL_AUDIO_F32;
            desiredAudioSpec.channels = 2;
            desiredAudioSpec.freq = 48000;
            SDL_AudioDeviceID audioDevice{SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desiredAudioSpec)};
            if (audioDevice == 0) {
                return std::unexpected(std::format("Couldn't open audio device: {}", SDL_GetError()));
            }
            int bufferSize{};
            if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desiredAudioSpec, &bufferSize)) {
                SDL_Log("WARN: Couldn't get audio device spec");
            } else {
                SDL_Log("Audio spec (freq, chanells, format, buffersize): %d, %d, %d, %d", desiredAudioSpec.freq, desiredAudioSpec.channels, desiredAudioSpec.format, bufferSize);
            }

            // GPU
            SDL_GPUDevice* gpu_dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true, nullptr);
            if (gpu_dev == nullptr) {
                return std::unexpected(std::format("Failed to create a GPU device: {}", SDL_GetError()));
            }
            SDL_ClaimWindowForGPUDevice(gpu_dev, window);
            SDL_Log("Created and binded gpu device to window");
            SDL_SetGPUSwapchainParameters(gpu_dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

            // Render context
            // Shaders
            static std::string vertexShaderName = "TexturedQuad.vert";
            static std::string fragmentShaderName = "TexturedQuad.frag";
            SDL_GPUShader* vertexShader{CreateShader(gpu_dev, std::ref(vertexShaderName), 0, 0, 0, 0)};
            SDL_GPUShader* fragmentShader{CreateShader(gpu_dev, std::ref(fragmentShaderName), 1, 0, 0, 0)};
            if (!vertexShader || !fragmentShader) {
                return std::unexpected("Failed to create shader");
            }
            SDL_Log("Loaded shaders: %s, %s", vertexShaderName.c_str(), fragmentShaderName.c_str());

            // Pipeline
            SDL_GPUColorTargetDescription colorTargetDescription{};
            colorTargetDescription.format = SDL_GetGPUSwapchainTextureFormat(gpu_dev, window);
            std::vector colorTargetDescriptions{colorTargetDescription};
            
            SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
            targetInfo.color_target_descriptions = colorTargetDescriptions.data();
            targetInfo.num_color_targets = colorTargetDescriptions.size();
            targetInfo.has_depth_stencil_target = false;

            SDL_GPUVertexBufferDescription vertexDescription{};
            vertexDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
            vertexDescription.instance_step_rate = 0;
            vertexDescription.slot = 0;
            vertexDescription.pitch = sizeof(PositionTextureVertex);
            std::vector vertexBufferDescriptions{vertexDescription};

            SDL_GPUVertexAttribute vertexAttributePosition{};
            vertexAttributePosition.buffer_slot = 0;
            vertexAttributePosition.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            vertexAttributePosition.location = 0;
            vertexAttributePosition.offset = 0;

            SDL_GPUVertexAttribute vertexAttrubuteTexture{};
            vertexAttrubuteTexture.buffer_slot = 0;
            vertexAttrubuteTexture.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            vertexAttrubuteTexture.location = 1;
            vertexAttrubuteTexture.offset = sizeof(float) * 3;

            std::vector vertexAttributes{vertexAttributePosition, vertexAttrubuteTexture};

            SDL_GPUVertexInputState inputState{};
            inputState.num_vertex_buffers = vertexBufferDescriptions.size();
            inputState.vertex_buffer_descriptions = vertexBufferDescriptions.data();
            inputState.num_vertex_attributes = vertexAttributes.size();
            inputState.vertex_attributes = vertexAttributes.data();

            SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo{};
            pipelineCreateInfo.vertex_shader = vertexShader;
            pipelineCreateInfo.fragment_shader = fragmentShader;
            pipelineCreateInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pipelineCreateInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pipelineCreateInfo.target_info = targetInfo;
            pipelineCreateInfo.vertex_input_state = inputState;
            
            SDL_GPUGraphicsPipeline* pipelineGraphics{SDL_CreateGPUGraphicsPipeline(gpu_dev, &pipelineCreateInfo)};
            if (!pipelineGraphics) {
                return std::unexpected(std::format("Failed to create the graphics pipeline: {}", SDL_GetError()));
            }
            SDL_Log("Created pipeline");
            // Clean up shaders
            SDL_ReleaseGPUShader(gpu_dev, vertexShader);
            SDL_ReleaseGPUShader(gpu_dev, fragmentShader);

            // images
            static std::string imageName{"background.bmp"};
            SDL_Surface* image{LoadImage(std::ref(imageName), 4)};
            if (!image) {
                return std::unexpected("Failed to read image");
            }
            
            // Sampler
            SDL_GPUSamplerCreateInfo samplerCreateInfo{};
            samplerCreateInfo.min_filter = SDL_GPU_FILTER_NEAREST;
            samplerCreateInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
            samplerCreateInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            samplerCreateInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            samplerCreateInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            samplerCreateInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            SDL_GPUSampler* sampler{SDL_CreateGPUSampler(gpu_dev, &samplerCreateInfo)};

            // create vertex buffer
            SDL_GPUBufferCreateInfo vertexBufferCreateInfo{};
            vertexBufferCreateInfo.size = sizeof(PositionTextureVertex) * 4;
            vertexBufferCreateInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

            SDL_GPUBuffer* vertexBuffer{SDL_CreateGPUBuffer(gpu_dev, &vertexBufferCreateInfo)};
            if (!vertexBuffer) {
                return std::unexpected(std::format("Failed to create the vertex buffer: %s", SDL_GetError()));
            }
            // SDL_SetGPUBufferName(gpu_dev, vertexBuffer, "A buffer");
            SDL_GPUBufferCreateInfo indexBufferCreateInfo{};
            indexBufferCreateInfo.size = sizeof(Uint16) * 6;
            indexBufferCreateInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            SDL_GPUBuffer* indexBuffer{SDL_CreateGPUBuffer(gpu_dev, &indexBufferCreateInfo)};

            // create transfer buffer to insert data into the vertex buffer, very intuative
            SDL_GPUTransferBufferCreateInfo transBufferCreateInfo{};
            transBufferCreateInfo.size = (sizeof(PositionTextureVertex) * 4) + (sizeof(Uint16) * 6);
            transBufferCreateInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

            SDL_GPUTransferBuffer* transferBuffer{SDL_CreateGPUTransferBuffer(gpu_dev, &transBufferCreateInfo)};
            if (!transferBuffer) {
                return std::unexpected(std::format("Failed to create the transfer buffer: %s", SDL_GetError()));
            }

            //Texture
            SDL_GPUTextureCreateInfo textureCreateInfo{};
            textureCreateInfo.type = SDL_GPU_TEXTURETYPE_2D;
            textureCreateInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            textureCreateInfo.width = image->w;
            textureCreateInfo.height = image->h;
            textureCreateInfo.layer_count_or_depth = 1;
            textureCreateInfo.num_levels = 1;
            textureCreateInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            SDL_GPUTexture* texture{SDL_CreateGPUTexture(gpu_dev, &textureCreateInfo)};

            // Data
            PositionTextureVertex* transferData{reinterpret_cast<PositionTextureVertex*>(SDL_MapGPUTransferBuffer(gpu_dev, transferBuffer, false))};
            if (!transferData) {
                return std::unexpected(std::format("Cound not aquire transfer data memory space: %s", SDL_GetError()));
            }
            transferData[0] = PositionTextureVertex{-1, 1,0,0,0};
            transferData[1] = PositionTextureVertex{ 1, 1,0,1,0};
            transferData[2] = PositionTextureVertex{ 1,-1,0,1,1};
            transferData[3] = PositionTextureVertex{-1,-1,0,0,1};

            Uint16* indexData = reinterpret_cast<Uint16*>(&transferData[4]);
            indexData[0] = 0;
            indexData[1] = 1;
            indexData[2] = 2;
            indexData[3] = 0;
            indexData[4] = 2;
            indexData[5] = 3;

            SDL_UnmapGPUTransferBuffer(gpu_dev, transferBuffer);

            SDL_GPUTransferBufferCreateInfo textureTransBufferCreateInfo{};
            textureTransBufferCreateInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            textureTransBufferCreateInfo.size = image->w * image->h * 4;
            SDL_GPUTransferBuffer* texureTranserBuffer{SDL_CreateGPUTransferBuffer(gpu_dev, &textureTransBufferCreateInfo)};

            Uint8* textureTranserPtr{reinterpret_cast<Uint8*>(SDL_MapGPUTransferBuffer(gpu_dev, texureTranserBuffer, false))};
            SDL_memcpy(textureTranserPtr, image->pixels, image->w * image->h * 4);
            SDL_UnmapGPUTransferBuffer(gpu_dev, texureTranserBuffer);
            
            // Uplead the buffer
            SDL_GPUCommandBuffer* uploadCommandBuffer{SDL_AcquireGPUCommandBuffer(gpu_dev)};
            if (!uploadCommandBuffer){
                return std::unexpected(std::format("Cound not aquire the upload command buffer: %s", SDL_GetError()));
            }
            SDL_GPUCopyPass* copyPass{SDL_BeginGPUCopyPass(uploadCommandBuffer)};
            
            SDL_GPUTransferBufferLocation transBufferLocationPosition{};
            transBufferLocationPosition.transfer_buffer = transferBuffer;
            transBufferLocationPosition.offset = 0;

            SDL_GPUBufferRegion bufferRegionPosition{};
            bufferRegionPosition.buffer = vertexBuffer;
            bufferRegionPosition.offset = 0;
            bufferRegionPosition.size = sizeof(PositionTextureVertex) * 4;

            SDL_UploadToGPUBuffer(copyPass, &transBufferLocationPosition, &bufferRegionPosition, false);

            SDL_GPUTransferBufferLocation transBufferLocationIndex{};
            transBufferLocationIndex.transfer_buffer = transferBuffer;
            transBufferLocationIndex.offset = sizeof(PositionTextureVertex) * 4;

            SDL_GPUBufferRegion bufferRegionIndex{};
            bufferRegionIndex.buffer = indexBuffer;
            bufferRegionIndex.offset = 0;
            bufferRegionIndex.size = sizeof(Uint16) * 6;

            SDL_UploadToGPUBuffer(copyPass, &transBufferLocationIndex, &bufferRegionIndex, false);

            SDL_GPUTextureTransferInfo textTransInfo{};
            textTransInfo.transfer_buffer = texureTranserBuffer;
            textTransInfo.offset = 0;
            textTransInfo.pixels_per_row = 0;
            textTransInfo.rows_per_layer = 0;

            SDL_GPUTextureRegion textTransReg{};
            textTransReg.texture = texture;
            textTransReg.w = image->w;
            textTransReg.h = image->h;
            textTransReg.d = 1;

            SDL_UploadToGPUTexture(copyPass, &textTransInfo, &textTransReg, false);

            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(uploadCommandBuffer);
            SDL_DestroySurface(image);
            SDL_ReleaseGPUTransferBuffer(gpu_dev, transferBuffer);
            SDL_ReleaseGPUTransferBuffer(gpu_dev, texureTranserBuffer);
            
            SDL_ShowWindow(window);            
            return std::unique_ptr<AppData>( new AppData(window, sounds, audioDevice, desiredAudioSpec, gpu_dev, pipelineGraphics, vertexBuffer, indexBuffer, texture, sampler));
        }
        ~AppData() {
            SDL_Log("Destroying app context");
            SDL_WaitForGPUIdle(gpu);
            SDL_ReleaseGPUGraphicsPipeline(gpu, pipelineGraphics);
            SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
            SDL_ReleaseGPUBuffer(gpu, indexBuffer);
            SDL_ReleaseGPUTexture(gpu, texture);
            SDL_ReleaseGPUSampler(gpu, sampler);
            SDL_ReleaseWindowFromGPUDevice(gpu, window);
            if (audioStream) {SDL_UnbindAudioStream(audioStream); SDL_DestroyAudioStream(audioStream);}
            SDL_CloseAudioDevice(audioDevice);
            SDL_DestroyGPUDevice(gpu);
            SDL_DestroyWindow(window);
            SDL_Log("App context destroyed");
        }
};



SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    // Systems
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)){
        SDL_Log("Couldn't init SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // inits
    auto appdata = AppData::New_AppData();
    if (appdata) {
        SDL_Log("Using backend: %s ", SDL_GetGPUDeviceDriver(appdata->get()->gpu));

        *appstate = appdata.value().release();
    } else {
        SDL_Log("%s", appdata.error().c_str());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto appdata = static_cast<AppData*>(appstate);

    if (appdata->audioStream && SDL_GetAudioStreamQueued(appdata->audioStream) == 0) {
        SDL_UnbindAudioStream(appdata->audioStream);
        SDL_DestroyAudioStream(appdata->audioStream);
        appdata->audioStream = nullptr;
    }

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(appdata->gpu);
    if (!command_buffer) {
        SDL_Log("Failed to aquire command buffer");
        return SDL_APP_CONTINUE;
    }
    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 swapchain_texture_width = 0;
    Uint32 swapchain_texture_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, appdata->window, &swapchain_texture, &swapchain_texture_width, &swapchain_texture_height)) {
        SDL_Log("Failed to aquire a swapchain texture: %s", SDL_GetError());
        SDL_SubmitGPUCommandBuffer(command_buffer);
        return SDL_APP_CONTINUE;
    }
    //SDL_Log("Swapchain Texture dim: %d %d", swapchain_texture_width, swapchain_texture_height);
    
    SDL_GPUColorTargetInfo color_target_infos = {};
    SDL_FColor color = {0.2f, 0.2f, 0.2f, 1.0f};
    color_target_infos.clear_color = color;
    color_target_infos.cycle = true;
    color_target_infos.cycle_resolve_texture = false;
    color_target_infos.mip_level = 0;
    color_target_infos.texture = swapchain_texture;
    color_target_infos.layer_or_depth_plane = 0;
    color_target_infos.store_op = SDL_GPU_STOREOP_STORE;
    color_target_infos.load_op = SDL_GPU_LOADOP_CLEAR;
    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_infos, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(render_pass, appdata->pipelineGraphics);
    
    SDL_GPUBufferBinding vertexBufferBinding{};
    vertexBufferBinding.buffer = appdata->vertexBuffer;
    vertexBufferBinding.offset = 0;
    std::vector vertexBufferBindings{vertexBufferBinding};
    SDL_BindGPUVertexBuffers(render_pass, 0, vertexBufferBindings.data(), vertexBufferBindings.size());
    
    SDL_GPUBufferBinding indexBufferBinding{};
    indexBufferBinding.buffer = appdata->indexBuffer;
    vertexBufferBinding.offset = 0;
    SDL_BindGPUIndexBuffer(render_pass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_GPUTextureSamplerBinding samperBinding{};
    samperBinding.texture = appdata->texture;
    samperBinding.sampler = appdata->sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &samperBinding, 1); // wrap in vec
    
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
    SDL_EndGPURenderPass(render_pass);

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        SDL_Log("Failed to sublit command buffer: %s", SDL_GetError());
        return SDL_APP_CONTINUE;
    }
    
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    auto appdata = static_cast<AppData*>(appstate);
    if (event->type == SDL_EVENT_QUIT) {
        SDL_Log("quit event reqistered");
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE){
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_SPACE) {
        SDL_UnbindAudioStream(appdata->audioStream);
        SDL_DestroyAudioStream(appdata->audioStream);
        appdata->audioStream = nullptr;
    }
    if (!appdata->audioStream && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        Quadrant q = GetClickQuadrant(appdata->window, event->button.x, event->button.y);
        switch(q) {
            case Quadrant::TopLeft:     {PlaySound(appdata->audioDevice, &appdata->audioDeviceSpec, &appdata->audioStream, appdata->sounds[0]); break;}
            case Quadrant::TopRight:    {PlaySound(appdata->audioDevice, &appdata->audioDeviceSpec, &appdata->audioStream, appdata->sounds[1]); break;}
            case Quadrant::BottomLeft:  {PlaySound(appdata->audioDevice, &appdata->audioDeviceSpec, &appdata->audioStream, appdata->sounds[2]); break;}
            case Quadrant::BottomRight: {PlaySound(appdata->audioDevice, &appdata->audioDeviceSpec, &appdata->audioStream, appdata->sounds[3]); break;}
        }
        return SDL_APP_CONTINUE;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    auto appdata = static_cast<AppData*>(appstate);
    delete appdata;
    SDL_Quit();
}
