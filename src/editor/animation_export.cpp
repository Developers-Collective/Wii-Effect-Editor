#include "animation_export.h"
#include "apng_writer.h"
#include "document.h"
#include "preview_view.h"
#include "../runtime/engine.h"
#include "../codec/effect.h"
#include <aurora/aurora.h>
#include <aurora/gfx.hpp>
#include <SDL3/SDL.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <numeric>
#include <set>
#include <unordered_map>
#include <thread>

namespace {
    namespace fs = std::filesystem;
    using breff::codec::Json;

    class Capture {
        unsigned size;
        wgpu::ComputePipeline pipeline;
        wgpu::Buffer storage, readback;
        wgpu::BindGroup group;
        aurora::gfx::EncoderTaskId task;
        std::shared_ptr<std::promise<std::vector<uint8_t>>> completion;

      public:
        explicit Capture(unsigned dimension) : size(dimension) {
            auto device = aurora::gfx::device();
            wgpu::Limits limits;
            device.GetLimits(&limits);
            const uint64_t bytes = uint64_t(size) * size * 4;
            if (size > limits.maxTextureDimension2D || bytes > limits.maxStorageBufferBindingSize ||
                bytes > limits.maxBufferSize)
                throw std::runtime_error("The selected image size exceeds the graphics device limits");
            wgpu::ShaderSourceWGSL source;
            source.code = R"(
                @group(0) @binding(0) var image: texture_2d<f32>;
                @group(0) @binding(1) var<storage, read_write> pixels: array<u32>;
                @compute @workgroup_size(8, 8)
                fn main(@builtin(global_invocation_id) id: vec3<u32>) {
                    let size = textureDimensions(image);
                    if (id.x < size.x && id.y < size.y) {
                        pixels[id.y * size.x + id.x] = pack4x8unorm(textureLoad(image, vec2<i32>(id.xy), 0));
                    }
                })";
            wgpu::ShaderModuleDescriptor shader;
            shader.nextInChain = &source;
            auto module = device.CreateShaderModule(&shader);
            wgpu::ComputePipelineDescriptor descriptor;
            descriptor.compute.module = module;
            descriptor.compute.entryPoint = "main";
            pipeline = device.CreateComputePipeline(&descriptor);
            wgpu::BufferDescriptor buffer;
            buffer.size = uint64_t(size) * size * 4;
            buffer.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
            storage = device.CreateBuffer(&buffer);
            buffer.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            readback = device.CreateBuffer(&buffer);
            aurora::gfx::EncoderTaskDescriptor encoderTask;
            encoderTask.label = "APNG readback";
            encoderTask.userdata = this;
            encoderTask.callback = [](const auto&, const wgpu::CommandEncoder& encoder, const void*, size_t,
                                      void* data) {
                auto& self = *static_cast<Capture*>(data);
                auto pass = encoder.BeginComputePass();
                pass.SetPipeline(self.pipeline);
                pass.SetBindGroup(0, self.group);
                pass.DispatchWorkgroups((self.size + 7) / 8, (self.size + 7) / 8);
                pass.End();
                encoder.CopyBufferToBuffer(self.storage, 0, self.readback, 0, uint64_t(self.size) * self.size * 4);
            };
            encoderTask.afterSubmit = [](const auto&, const void*, size_t, void* data) {
                auto& self = *static_cast<Capture*>(data);
                self.readback.MapAsync(
                    wgpu::MapMode::Read, 0, uint64_t(self.size) * self.size * 4, wgpu::CallbackMode::AllowSpontaneous,
                    [buffer = self.readback, size = self.size,
                     completion = self.completion](wgpu::MapAsyncStatus status, wgpu::StringView) {
                        if (status != wgpu::MapAsyncStatus::Success) {
                            completion->set_exception(
                                std::make_exception_ptr(std::runtime_error("GPU readback failed")));
                            return;
                        }
                        const auto* pixels = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
                        std::vector<uint8_t> result(pixels, pixels + size_t(size) * size * 4);
                        buffer.Unmap();
                        completion->set_value(std::move(result));
                    });
            };
            task = aurora::gfx::register_encoder_task_type(encoderTask);
        }

        ~Capture() {
            aurora::gfx::synchronize();
            aurora::gfx::unregister_encoder_task_type(task);
        }

        std::vector<uint8_t> finishFrame() {
            aurora::gfx::ResolvedTargets resolved;
            if (!aurora::gfx::resolve_pass({true, false}, resolved))
                throw std::runtime_error("Could not resolve the APNG render target");
            std::array<wgpu::BindGroupEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].textureView = resolved.color;
            entries[1].binding = 1;
            entries[1].buffer = storage;
            entries[1].size = uint64_t(size) * size * 4;
            wgpu::BindGroupDescriptor descriptor;
            descriptor.layout = pipeline.GetBindGroupLayout(0);
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.data();
            group = aurora::gfx::device().CreateBindGroup(&descriptor);
            completion = std::make_shared<std::promise<std::vector<uint8_t>>>();
            auto ready = completion->get_future();
            if (!aurora::gfx::push_encoder_task(task, nullptr, 0))
                throw std::runtime_error("Could not queue APNG readback");
            aurora_end_frame();
            aurora::gfx::synchronize();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            auto instance = aurora::gfx::device().GetAdapter().GetInstance();
            while (ready.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                instance.ProcessEvents();
                if (std::chrono::steady_clock::now() > deadline)
                    throw std::runtime_error("Timed out reading the APNG render target");
                SDL_Delay(1);
            }
            return ready.get();
        }
    };

    std::string utf8(const fs::path& path) {
        const auto value = path.u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    std::string filename(std::string name) {
        for (auto& c : name)
            if (static_cast<unsigned char>(c) < 0x20 ||
                std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                c = '_';
        while (!name.empty() && (name.back() == '.' || name.back() == ' '))
            name.pop_back();
        return name.empty() ? "effect" : name;
    }

    using Dependencies = std::map<std::string, std::map<std::string, bool>>;

    void children(const Json& value, std::map<std::string, bool>& result) {
        if (value.is_object()) {
            if (value.contains("childType") && value.contains("name") && value["name"].is_string())
                result[value["name"].get<std::string>()] |= value["childType"] == "Emitter";
            for (const auto& child : value)
                children(child, result);
        } else if (value.is_array())
            for (const auto& child : value)
                children(child, result);
    }

    struct Timing {
        bool loop = false;
        unsigned warmup = 0, period = 1;
    };

    Timing timing(breff::Archive& archive, const Dependencies& dependencies, const std::string& name) {
        Timing result;
        std::set<std::pair<std::string, bool>> seen;
        std::vector<std::pair<std::string, bool>> pending{{name, true}};
        unsigned lifetime = 1;
        while (!pending.empty()) {
            const auto [current, createsEmitter] = pending.back();
            pending.pop_back();
            if (!seen.emplace(current, createsEmitter).second || !archive.effects.contains(current))
                continue;
            const auto& resource = archive.effects.at(current)->resource;
            const auto& emitter = resource.emitter;
            const bool infinite = createsEmitter && (emitter.commonFlag & nw4r::ef::EmitterDesc::CMN_FLAG_MAX_LIFE);
            result.loop |= infinite;
            lifetime = std::max(lifetime, unsigned(emitter.ptclLife));
            result.warmup = std::max(result.warmup, unsigned(emitter.emitEmitStart) + emitter.ptclLife);
            auto combine = [&](unsigned period) {
                const uint64_t combined = uint64_t(result.period) / std::gcd(result.period, period) * period;
                if (combined > 1000000)
                    throw std::runtime_error("Combined animation period exceeds one million frames");
                result.period = unsigned(combined);
            };
            if (infinite) {
                combine(unsigned(emitter.emitEmitInterval) + 1);
                for (const auto& track : resource.emitterTracks) {
                    if (track.size() < 0x20 || !(track[4] & nw4r::ef::AnimCurveHeader::PROC_FLAG_INFLOOP))
                        continue;
                    unsigned length = (unsigned(track[8]) << 8) | track[9];
                    const bool turn = track[4] & nw4r::ef::AnimCurveHeader::PROC_FLAG_TURN;
                    if (length > 1 && (track[2] != nw4r::ef::AC_TYPE_PARTICLE_TEXTURE || turn))
                        --length;
                    combine(std::max(1u, length) * (turn ? 2 : 1));
                }
            }
            if (auto found = dependencies.find(current); found != dependencies.end())
                pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
        // With no longer authored emitter cycle, cover the complete particle lifetime.
        if (result.loop)
            result.period *= std::max(1u, (lifetime + result.period - 1) / result.period);
        return result;
    }

    struct Animation {
        unsigned begin = 0, end = 1;
        bool loop = false;
        breff::Engine::Bounds bounds;
    };

    Animation measure(breff::Engine& engine, breff::Archive& archive, const std::string& name, const Timing& timing,
                      bool top) {
        Animation result;
        result.loop = timing.loop;
        result.begin = timing.loop ? timing.warmup : 0;
        result.end = timing.loop ? result.begin + timing.period : 1000000;
        engine.start(archive, name, uint16_t(archive.effects.at(name)->resource.emitter.randomSeed));
        OrbitCamera camera;
        camera.pitch = top ? 1.5707963267948966f : 0;
        nw4r::ef::DrawInfo view;
        nw4r::math::MTX34 matrix;
        camera.view(matrix.m);
        view.SetViewMtx(matrix);
        engine.setView(view);
        std::vector<uint64_t> signatures;
        breff::Engine::Bounds fullBounds;
        const auto limit = result.loop ? result.begin + timing.period * 2 : result.end;
        for (unsigned tick = 0; tick < limit; ++tick) {
            engine.step();
            const auto bounds = engine.bounds();
            fullBounds.include(bounds);
            if (tick >= result.begin) {
                result.bounds.include(bounds);
                if (result.loop)
                    signatures.push_back(engine.visualSignature());
            }
            if (engine.finished()) {
                result.end = tick + 1;
                result.begin = 0;
                result.loop = false;
                result.bounds = fullBounds;
                return result;
            }
        }
        if (!timing.loop)
            throw std::runtime_error("Effect did not finish after one million simulation frames");
        // Find the shortest observed period and require two complete matching cycles.
        std::vector<size_t> prefix(signatures.size());
        for (size_t i = 1; i < signatures.size(); ++i) {
            size_t j = prefix[i - 1];
            while (j && signatures[i] != signatures[j])
                j = prefix[j - 1];
            if (signatures[i] == signatures[j])
                ++j;
            prefix[i] = j;
        }
        if (!signatures.empty()) {
            const auto period = signatures.size() - prefix.back();
            if (period <= signatures.size() / 2 && signatures.size() % period == 0)
                result.end = result.begin + unsigned(period);
        }
        return result;
    }

    void setupView(unsigned size, const breff::Engine::Bounds& bounds, bool top, nw4r::ef::DrawInfo& info) {
        OrbitCamera camera;
        camera.pitch = top ? 1.5707963267948966f : 0;
        std::array<float, 3> center{};
        float radius = 1;
        if (!bounds.empty) {
            for (unsigned i = 0; i < 3; ++i)
                center[i] = (bounds.minimum[i] + bounds.maximum[i]) * .5f;
            radius = std::max({bounds.maximum[0] - bounds.minimum[0],
                               bounds.maximum[top ? 2 : 1] - bounds.minimum[top ? 2 : 1], .01f}) *
                     .55f;
        }
        const unsigned depthAxis = top ? 1 : 2;
        const float depth = bounds.empty ? 1 : bounds.maximum[depthAxis] - bounds.minimum[depthAxis];
        camera.distance = std::max(1.f, depth * 2 + radius * 2);
        drawPreviewGrid({0, 0, float(size), float(size)}, camera, false, false);
        Mtx44 projection;
        C_MTXOrtho(projection, radius, -radius, -radius, radius, .001f, camera.distance + depth + radius * 2);
        GXSetProjection(projection, GX_ORTHOGRAPHIC);
        nw4r::math::MTX34 view;
        camera.view(view.m);
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                view.m[row][3] -= view.m[row][column] * center[column];
        info.SetViewMtx(view);
    }

    void whiteBackground(unsigned size) {
        OrbitCamera camera;
        drawPreviewGrid({0, 0, float(size), float(size)}, camera, false, false);
        Mtx44 projection;
        C_MTXOrtho(projection, 1, -1, -1, 1, 0, 1);
        GXSetProjection(projection, GX_ORTHOGRAPHIC);
        Mtx identity;
        PSMTXIdentity(identity);
        GXLoadPosMtxImm(identity, GX_PNMTX0);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        for (const auto& point : std::array<std::array<float, 2>, 4>{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}}) {
            GXPosition3f32(point[0], point[1], -.5f);
            GXColor4u8(255, 255, 255, 255);
        }
        GXEnd();
    }

    void render(breff::Engine& engine, breff::Archive& archive, Capture& capture, const std::string& name,
                const fs::path& destination, unsigned size, const Animation& animation, bool top) {
        const auto temporary = fs::path(destination).concat(".tmp");
        try {
            breff::ApngWriter png(temporary, size, animation.end - animation.begin);
            engine.start(archive, name, uint16_t(archive.effects.at(name)->resource.emitter.randomSeed));
            nw4r::ef::DrawInfo info;
            OrbitCamera camera;
            camera.pitch = top ? 1.5707963267948966f : 0;
            nw4r::math::MTX34 initialView;
            camera.view(initialView.m);
            info.SetViewMtx(initialView);
            engine.setView(info);
            for (unsigned tick = 0; tick < animation.end; ++tick) {
                engine.step();
                if (tick < animation.begin)
                    continue;
                aurora_update();
                if (!aurora_begin_frame())
                    throw std::runtime_error("Could not begin headless rendering");
                if (!aurora::gfx::create_pass(size, size))
                    throw std::runtime_error("Could not create the APNG render target");
                setupView(size, animation.bounds, top, info);
                engine.draw(info);
                auto pixels = capture.finishFrame();
                if (!aurora_begin_frame() || !aurora::gfx::create_pass(size, size))
                    throw std::runtime_error("Could not begin transparency capture");
                whiteBackground(size);
                setupView(size, animation.bounds, top, info);
                engine.draw(info);
                const auto white = capture.finishFrame();
                for (size_t i = 0; i < pixels.size(); i += 4) {
                    // Black/white captures recover coverage even when GX alpha writes are
                    // disabled. Additive light contributes its minimum representable alpha.
                    int coverage = 0;
                    for (unsigned c = 0; c < 3; ++c)
                        coverage = std::max(coverage, 255 - int(white[i + c]) + int(pixels[i + c]));
                    const unsigned alpha = unsigned(std::clamp(coverage, 0, 255));
                    pixels[i + 3] = uint8_t(alpha);
                    if (alpha)
                        for (unsigned c = 0; c < 3; ++c)
                            pixels[i + c] = uint8_t(std::min(255u, unsigned(pixels[i + c]) * 255 / alpha));
                }
                png.frame(pixels);
            }
            png.finish();
            if (!SDL_RenamePath(utf8(temporary).c_str(), utf8(destination).c_str()))
                throw std::runtime_error(std::string("Could not finish APNG export: ") + SDL_GetError());
        } catch (...) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw;
        }
    }
}

std::optional<int> exportAnimations(int argc, char** argv) {
    bool requested = false;
    for (int i = 1; i < argc; ++i)
        requested |= std::string_view(argv[i]) == "--export-apngs";
    if (!requested)
        return std::nullopt;
    bool initialized = false;
    try {
        fs::path input, textures, output;
        std::vector<fs::path> textureFolders;
        std::string selected;
        unsigned size = 1024;
        unsigned jobs = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
        unsigned worker = 0, workers = 1;
        bool child = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc)
                    throw std::runtime_error("Missing value for " + argument);
                return argv[i];
            };
            if (argument == "--export-apngs")
                continue;
            if (argument == "--output")
                output = fs::u8path(value());
            else if (argument == "--breft")
                textures = fs::u8path(value());
            else if (argument == "--texture-folder")
                textureFolders.push_back(fs::u8path(value()));
            else if (argument == "--size") {
                const auto text = value();
                size_t consumed = 0;
                const auto parsed = std::stoul(text, &consumed);
                if (consumed != text.size() || parsed < 512 || parsed > 8192)
                    throw std::runtime_error("APNG size must be between 512 and 8192 pixels");
                size = unsigned(parsed);
            } else if (argument == "--jobs" || argument == "--apng-worker" || argument == "--apng-workers") {
                const auto text = value();
                size_t consumed = 0;
                const auto parsed = std::stoul(text, &consumed);
                if (consumed != text.size() || parsed > 64 || (argument != "--apng-worker" && parsed == 0))
                    throw std::runtime_error("Worker count must be between 1 and 64");
                if (argument == "--jobs")
                    jobs = unsigned(parsed);
                else if (argument == "--apng-workers")
                    workers = unsigned(parsed);
                else {
                    worker = unsigned(parsed);
                    child = true;
                }
            } else if (argument == "--effect")
                selected = value();
            else if (!argument.starts_with("--") && input.empty())
                input = fs::u8path(argument);
            else
                throw std::runtime_error("Unknown APNG export argument: " + argument);
        }
        if (input.empty() || output.empty())
            throw std::runtime_error(
                "Usage: EffectEditor --export-apngs FILE.breff --output FOLDER [--breft FILE.breft] [--texture-folder FOLDER] [--size 1024] [--jobs COUNT] [--effect NAME]");
        for (const auto& folder : textureFolders)
            if (!fs::is_directory(folder))
                throw std::runtime_error("Texture folder does not exist: " + utf8(folder));
        if (worker >= workers)
            throw std::runtime_error("Invalid APNG worker index");
        if (!child && jobs > 1 && selected.empty()) {
            std::vector<SDL_Process*> processes;
            try {
                for (unsigned index = 0; index < jobs; ++index) {
                    std::vector<std::string> arguments;
                    for (int i = 0; i < argc; ++i)
                        arguments.emplace_back(argv[i]);
                    arguments.insert(arguments.end(),
                                     {"--apng-worker", std::to_string(index), "--apng-workers", std::to_string(jobs)});
                    std::vector<const char*> pointers;
                    for (const auto& argument : arguments)
                        pointers.push_back(argument.c_str());
                    pointers.push_back(nullptr);
                    auto* process = SDL_CreateProcess(pointers.data(), false);
                    if (!process)
                        throw std::runtime_error(std::string("Could not start APNG worker: ") + SDL_GetError());
                    processes.push_back(process);
                }
                int result = 0;
                for (auto* process : processes) {
                    int status = 0;
                    if (!SDL_WaitProcess(process, true, &status) || status != 0)
                        result = 1;
                }
                for (auto* process : processes)
                    SDL_DestroyProcess(process);
                return result;
            } catch (...) {
                for (auto* process : processes) {
                    SDL_KillProcess(process, true);
                    SDL_WaitProcess(process, true, nullptr);
                    SDL_DestroyProcess(process);
                }
                throw;
            }
        }
        breff::DocumentService document;
        auto response = document.handle({{"op", "open"}, {"breff", utf8(input)}, {"breft", utf8(textures)}});
        if (!response.value("ok", false))
            throw std::runtime_error(response.value("error", "Could not open BREFF"));
        for (const auto& folder : textureFolders) {
            response = document.handle({{"op", "preview_texture_folder"}, {"path", utf8(folder)}});
            if (!response.value("ok", false))
                throw std::runtime_error("Could not load texture folder " + utf8(folder) + ": " +
                                         response.value("error", "Unknown error"));
        }
        const auto& state = response.at("data");
        if (!state.at("errors").empty())
            throw std::runtime_error("Some effects could not be decoded: " + state.at("errors").dump());
        breff::Archive archive;
        const auto snapshot = fs::u8path(state.at("snapshot").get<std::string>());
        archive.load(snapshot, fs::u8path(state.at("texturePath").get<std::string>()), state.at("version"));
        if (!selected.empty() && !archive.effects.contains(selected))
            throw std::runtime_error("Unknown effect: " + selected);
        std::ifstream file(snapshot, std::ios::binary);
        const breff::codec::Bytes bytes{std::istreambuf_iterator<char>(file), {}};
        const auto effects = breff::codec::ResourceFile::decode(bytes);
        Dependencies dependencies;
        for (const auto& effect : effects.entries)
            children(breff::codec::decodeEffect(effect.data, effects.version), dependencies[effect.name]);
        fs::create_directories(output);

        SDL_SetHint("EFFECT_EDITOR_HEADLESS", "1");
        AuroraConfig config{};
        config.appName = "Effect Editor";
        char* preferences = SDL_GetPrefPath(nullptr, config.appName);
        if (!preferences)
            throw std::runtime_error("Could not locate the renderer cache directory");
        const auto cache = fs::u8path(preferences) / "apng-cache" / std::to_string(worker);
        SDL_free(preferences);
        fs::create_directories(cache);
        const auto cachePath = utf8(cache);
        config.cachePath = cachePath.c_str();
        config.windowWidth = config.windowHeight = 512;
        config.desiredBackend = BACKEND_AUTO;
        config.allowCpuAdapter = true;
        config.logLevel = LOG_ERROR;
        config.logCallback = [](AuroraLogLevel level, const char*, const char* message, unsigned length) {
            if (level >= LOG_ERROR)
                std::fprintf(stderr, "%.*s\n", int(length), message);
        };
        aurora_initialize(1, argv, &config);
        initialized = true;
        GXInit(nullptr, 0);
        {
            Capture capture(size);
            breff::Engine engine;
            std::set<std::string> usedNames;
            unsigned index = 0;
            for (const auto& effect : effects.entries) {
                if (!selected.empty() && effect.name != selected)
                    continue;
                const auto base = filename(effect.name);
                auto unique = base;
                for (unsigned suffix = 2; !usedNames.insert(unique).second; ++suffix)
                    unique = base + "_" + std::to_string(suffix);
                if (index++ % workers != worker)
                    continue;
                const auto duration = timing(archive, dependencies, effect.name);
                for (const bool top : {false, true}) {
                    const auto animation = measure(engine, archive, effect.name, duration, top);
                    render(engine, archive, capture, effect.name,
                           output / fs::u8path(unique + (top ? "_top.png" : "_front.png")), size, animation, top);
                }
            }
        }
        aurora_shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "APNG export failed: %s\n", error.what());
        if (initialized)
            aurora_shutdown();
        return 1;
    }
}
