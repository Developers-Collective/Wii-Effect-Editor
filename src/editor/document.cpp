#include "document.h"
#include "../codec/effect.h"
#include "../codec/compatibility.h"
#include "../codec/animation.h"
#include "../codec/texture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <cctype>
#include <png.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace breff {
using codec::Json;
using codec::Bytes;
namespace fs = std::filesystem;
namespace {
Bytes readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open " + path.string());
    auto length = input.tellg();
    if (length < 0 || length > 256 * 1024 * 1024)
        throw std::runtime_error("File exceeds 256 MiB");
    Bytes bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
        throw std::runtime_error("Could not read " + path.string());
    return bytes;
}
std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void atomicWrite(const fs::path& path, std::span<const uint8_t> bytes) {
    static std::atomic<unsigned> serial = 0;
    auto temporary = path;
    temporary += std::string(".") + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                 std::to_string(serial++) + ".tmp";
    try {
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()) || !stream.flush())
                throw std::runtime_error("Could not write " + path.string());
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace " + path.string());
#else
        fs::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}
void writePair(const fs::path& effect, const Bytes& effectBytes, const fs::path& texture, const Bytes& textureBytes) {
    if (fs::absolute(effect).lexically_normal() == fs::absolute(texture).lexically_normal())
        throw std::runtime_error("BREFF and BREFT need different paths");
    const bool existed = fs::exists(texture);
    const auto before = existed ? readFile(texture) : Bytes();
    atomicWrite(texture, textureBytes);
    try {
        atomicWrite(effect, effectBytes);
    } catch (...) {
        if (existed)
            atomicWrite(texture, before);
        else
            fs::remove(texture);
        throw;
    }
}
void validateName(const std::string& name, const codec::ResourceFile& archive) {
    if (name.empty() || name.size() > 65534 || name.find('\0') != std::string::npos)
        throw std::runtime_error("Enter a nonempty resource name");
    for (const auto& entry : archive.entries)
        if (entry.name == name)
            throw std::runtime_error("Resource name already exists");
}
template <class F> void visit(Json& value, const std::string& path, F&& fn) {
    if (value.is_object()) {
        fn(value, path);
        for (auto it = value.begin(); it != value.end(); ++it)
            visit(it.value(), path + "/" + it.key(), fn);
    } else if (value.is_array())
        for (size_t i = 0; i < value.size(); ++i)
            visit(value[i], path + "/" + std::to_string(i), fn);
}
bool textureSlot(const std::string& path) {
    return path.ends_with("/texture1") || path.ends_with("/texture2") || path.ends_with("/textureInd");
}
std::string textureKey(const Json& value, const std::string& path) {
    if (value.contains("textureName"))
        return "textureName";
    if (textureSlot(path) && value.contains("name"))
        return "name";
    return "";
}
void validateExportName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 32;
        }))
        throw std::runtime_error("Resource name cannot be used as a filename: " + name);
}
Json readEffectFiles(const fs::path& path) {
    const auto bytes = readFile(path);
    Json result = {
        {"name", utf8(path.stem())}, {"effect", Json::parse(bytes.begin(), bytes.end())}, {"effects", Json::array()}};
    std::set<std::string> seen{utf8(path.stem())};
    std::vector<Json> pending{result.at("effect")};
    for (size_t i = 0; i < pending.size(); ++i) {
        auto value = pending[i];
        visit(value, "", [&](Json& object, const std::string&) {
            if (!object.contains("childType") || !object.contains("name") || !object.at("name").is_string())
                return;
            const auto name = object.at("name").get<std::string>();
            if (name.empty() || !seen.insert(name).second)
                return;
            validateExportName(name);
            const auto childPath = path.parent_path() / fs::u8path(name + ".json");
            if (!fs::is_regular_file(childPath))
                throw std::runtime_error("Missing child effect JSON: " + utf8(childPath));
            const auto childBytes = readFile(childPath);
            auto child = Json::parse(childBytes.begin(), childBytes.end());
            result["effects"].push_back({{"name", name}, {"effect", child}});
            pending.push_back(std::move(child));
        });
    }
    return result;
}
Bytes importImage(const fs::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    if (extension == ".png") {
        const auto bytes = readFile(path);
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        struct Release {
            png_image* image;
            ~Release() {
                png_image_free(image);
            }
        } release{&image};
        if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
            throw std::runtime_error("Could not read PNG: " + std::string(image.message));
        if (!image.width || !image.height || image.width > 1024 || image.height > 1024)
            throw std::runtime_error("GX images must be between 1 and 1024 pixels per dimension");
        image.format = PNG_FORMAT_RGBA;
        Bytes pixels(PNG_IMAGE_SIZE(image));
        if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
            throw std::runtime_error("Could not decode PNG: " + std::string(image.message));
        return codec::encodeTexture(image.width, image.height, pixels);
    }
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Uninit {
        bool active;
        ~Uninit() {
            if (active)
                CoUninitialize();
        }
    } guard{SUCCEEDED(initialized)};
    auto check = [](HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("Could not decode image");
    };
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                             &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0;
    check(frame->GetSize(&width, &height));
    if (!width || !height || width > 1024 || height > 1024)
        throw std::runtime_error("GX images must be between 1 and 1024 pixels per dimension");
    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom));
    Bytes pixels(width * height * 4);
    check(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
    return codec::encodeTexture(width, height, pixels);
#else
    throw std::runtime_error("Choose a PNG image");
#endif
}
}
DocumentService::DocumentService() {
    const auto root = fs::temp_directory_path();
    for (unsigned i = 0; i < 100; ++i) {
        directory =
            root / ("breff-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(i));
        if (fs::create_directory(directory))
            return;
    }
    throw std::runtime_error("Could not create editor session directory");
}
DocumentService::~DocumentService() {
    std::error_code ec;
    fs::remove_all(directory, ec);
}
codec::ResourceFile DocumentService::effectArchive(const State& state, unsigned version) const {
    auto archive = state.effects;
    archive.version = uint16_t(version ? version : state.version);
    for (auto& entry : archive.entries) {
        if (state.values.contains(entry.name)) {
            const auto& value = state.values.at(entry.name);
            const bool unchanged =
                state.originalValues.contains(entry.name) && state.originalValues.at(entry.name) == value;
            const bool compatibleLayout =
                archive.version == state.effects.version || (archive.version >= 8 && state.effects.version >= 8);
            if (!unchanged || !compatibleLayout) {
                const auto projected = codec::projectEffect(value, archive.version).value;
                entry.data = compatibleLayout ? codec::encodeEffectPreserving(projected, archive.version, entry.data)
                                              : codec::encodeEffect(projected, archive.version);
            }
        } else if (archive.version != state.effects.version)
            throw std::runtime_error("Cannot convert unreadable effect " + entry.name);
    }
    return archive;
}
codec::ResourceFile DocumentService::textureArchive(const State& state) const {
    auto archive = state.textures;
    archive.version = uint16_t(state.version == originalVersion ? originalTextureVersion : state.version);
    return archive;
}
void DocumentService::publish(const State& candidate, const std::map<std::string, Bytes>* overrides) {
    // Render the actual destination representation through the runtime adapter.
    auto effects = effectArchive(candidate);
    if (effects.version != 11) {
        for (auto& entry : effects.entries)
            entry.data = codec::encodeEffect(codec::decodeEffect(entry.data, effects.version, true), 11);
        effects.version = 11;
    }
    auto textures = candidate.textures;
    textures.version = 11;
    Json metadata = Json::array();
    Json dependencies = Json::object();
    auto values = candidate.values;
    for (auto& value : values)
        value = codec::projectEffect(value, candidate.version).value;
    for (auto it = values.begin(); it != values.end(); ++it)
        visit(it.value(), "", [&](Json& value, const std::string& path) {
            const auto key = textureKey(value, path);
            if (!key.empty() && value.at(key).is_string()) {
                const auto name = value.at(key).get<std::string>();
                if (!dependencies.contains(name))
                    dependencies[name] = Json::array();
                dependencies[name].push_back({{"effect", it.key()}, {"field", path + "/" + key}});
            }
        });
    for (size_t i = 0; i < textures.entries.size(); ++i) {
        const auto& entry = textures.entries[i];
        auto cached = imageCache.find(entry.data);
        if (cached == imageCache.end()) {
            auto pixels = codec::decodeTexture(entry.data);
            const auto path = directory / ("texture-" + std::to_string(imageCache.size()) + ".rgba");
            atomicWrite(path, pixels.rgba);
            cached = imageCache
                         .emplace(entry.data, Json{{"width", pixels.width},
                                                   {"height", pixels.height},
                                                   {"format", pixels.format},
                                                   {"image", utf8(path)},
                                                   {"error", ""}})
                         .first;
        }
        auto item = cached->second;
        item["name"] = entry.name;
        item["dependencies"] = dependencies.value(entry.name, Json::array());
        metadata.push_back(item);
    }
    const auto& replacements = overrides ? *overrides : previewTextures;
    Json missing = Json::array();
    std::set<std::string> available;
    for (const auto& entry : textures.entries)
        available.insert(entry.name);
    Bytes black(4 * 4 * 4, 0);
    for (size_t i = 3; i < black.size(); i += 4)
        black[i] = 255;
    const auto blackTexture = codec::encodeTexture(4, 4, black);
    for (const auto& [name, users] : dependencies.items())
        if (!name.empty() && !available.contains(name)) {
            if (auto image = replacements.find(name); image != replacements.end())
                textures.entries.push_back({name, image->second});
            else {
                missing.push_back(name);
                textures.entries.push_back({name, blackTexture});
            }
        }
    writePair(directory / "preview.breff", effects.encode(), directory / "preview.breft", textures.encode());
    textureMetadata = std::move(metadata);
    missingTextures = std::move(missing);
    ++generation;
}
Json DocumentService::state() const {
    if (!document.loaded)
        return {{"loaded", false}};
    Json names = Json::array();
    for (const auto& entry : document.effects.entries)
        names.push_back(entry.name);
    Json effect = document.values.value(document.selected, Json());
    if (!effect.is_null())
        effect = codec::projectEffect(effect, document.version).value;
    return {{"loaded", true},
            {"path", utf8(effectPath)},
            {"dirty",
             effectArchive(document).encode() != savedEffects || textureArchive(document).encode() != savedTextures},
            {"project", document.effects.projectName},
            {"version", document.version},
            {"originalVersion", originalVersion},
            {"originalTextureVersion", originalTextureVersion},
            {"names", names},
            {"errors", document.errors},
            {"selected", document.selected},
            {"effect", effect},
            {"texturePath", utf8(directory / "preview.breft")},
            {"textureSourcePath", utf8(texturePath)},
            {"textures", textureMetadata},
            {"textureCount", textureMetadata.size()},
            {"missingTextures", missingTextures},
            {"previewTextureFiles", previewTexturePaths},
            {"snapshot", utf8(directory / "preview.breff")},
            {"generation", generation},
            {"canUndo", !undo.empty()},
            {"canRedo", !redo.empty()}};
}
Json DocumentService::dispatch(const Json& request) {
    const std::string op = request.at("op");
    if (op == "state")
        return state();
    if (op == "open") {
        State candidate;
        const auto path = fs::absolute(fs::u8path(request.at("breff").get<std::string>()));
        auto tex = path;
        tex.replace_extension(".breft");
        if (request.contains("breft") && request["breft"].is_string() && !request["breft"].get<std::string>().empty())
            tex = fs::absolute(fs::u8path(request["breft"].get<std::string>()));
        candidate.effects = codec::ResourceFile::decode(readFile(path));
        candidate.textures = codec::ResourceFile::decode(readFile(tex));
        candidate.version = candidate.effects.version;
        if (candidate.effects.magic != "REFF" || candidate.textures.magic != "REFT")
            throw std::runtime_error("Choose a BREFF and a BREFT");
        for (const auto& entry : candidate.effects.entries) {
            try {
                candidate.values[entry.name] = codec::decodeEffect(entry.data, candidate.effects.version, true);
            } catch (const std::exception& e) {
                candidate.errors[entry.name] = e.what();
            }
        }
        candidate.originalValues = candidate.values;
        if (!candidate.effects.entries.empty())
            candidate.selected = candidate.effects.entries.front().name;
        candidate.loaded = true;
        const std::map<std::string, Bytes> emptyOverrides;
        publish(candidate, &emptyOverrides);
        previewTextures.clear();
        previewTexturePaths.clear();
        document = std::move(candidate);
        originalVersion = document.effects.version;
        originalTextureVersion = document.textures.version;
        effectPath = path;
        texturePath = tex;
        savedEffects = effectArchive(document).encode();
        savedTextures = document.textures.encode();
        undo.clear();
        redo.clear();
        return state();
    }
    if (!document.loaded)
        throw std::runtime_error("Open a BREFF/BREFT pair first");
    if (op == "preview_texture_clear") {
        const std::map<std::string, Bytes> empty;
        publish(document, &empty);
        previewTextures.clear();
        previewTexturePaths.clear();
        return state();
    }
    if (op == "preview_texture_folder") {
        const auto folder = fs::u8path(request.at("path").get<std::string>());
        if (!fs::is_directory(folder))
            throw std::runtime_error("Choose a folder containing PNG textures");
        std::map<std::string, fs::path> images;
        for (const auto& entry : fs::directory_iterator(folder))
            if (entry.is_regular_file()) {
                auto extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return char(std::tolower(c));
                });
                if (extension == ".png")
                    images.emplace(utf8(entry.path().stem()), entry.path());
            }
        auto replacements = previewTextures;
        auto paths = previewTexturePaths;
        size_t matched = 0;
        for (const auto& item : missingTextures) {
            const auto name = item.get<std::string>();
            if (auto image = images.find(name); image != images.end()) {
                replacements[name] = importImage(image->second);
                paths[name] = utf8(image->second);
                ++matched;
            }
        }
        if (matched) {
            publish(document, &replacements);
            previewTextures = std::move(replacements);
            previewTexturePaths = std::move(paths);
        }
        auto result = state();
        result["previewTexturesMatched"] = matched;
        return result;
    }
    if (op == "select") {
        auto name = request.at("name").get<std::string>();
        if (std::none_of(document.effects.entries.begin(), document.effects.entries.end(), [&](const auto& e) {
                return e.name == name;
            }))
            throw std::runtime_error("Unknown effect");
        document.selected = name;
        return state();
    }
    if (op == "undo" || op == "redo") {
        auto& source = op == "undo" ? undo : redo;
        auto& target = op == "undo" ? redo : undo;
        if (!source.empty()) {
            publish(source.back());
            target.push_back(document);
            document = std::move(source.back());
            source.pop_back();
        }
        return state();
    }
    if (op == "import_inspect" || op == "import_scan") {
        const auto packet =
            op == "import_inspect" ? readEffectFiles(fs::u8path(request.at("path").get<std::string>())) : request;
        const auto effect = packet.at("effect");
        const auto effects = packet.value("effects", Json::array());
        std::set<std::string> required, available;
        for (const auto& entry : document.textures.entries)
            available.insert(entry.name);
        auto inspect = [&](Json value) {
            codec::encodeEffect(value, 11);
            visit(value, "", [&](Json& object, const std::string& path) {
                const auto key = textureKey(object, path);
                if (!key.empty() && object.at(key).is_string()) {
                    const auto name = object.at(key).get<std::string>();
                    if (!name.empty() && !available.contains(name))
                        required.insert(name);
                }
            });
        };
        inspect(effect);
        for (const auto& child : effects)
            inspect(child.at("effect"));
        const auto folders = request.value("folders", Json::array());
        Json matches = Json::object(), errors = Json::object();
        for (const auto& folder : folders) {
            const auto path = fs::u8path(folder.get<std::string>());
            if (!fs::is_directory(path))
                throw std::runtime_error("Texture folder does not exist: " + utf8(path));
            for (const auto& entry : fs::directory_iterator(path)) {
                if (!entry.is_regular_file())
                    continue;
                auto extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return char(std::tolower(c));
                });
                if (extension != ".png")
                    continue;
                const auto name = utf8(entry.path().stem());
                if (!required.contains(name) || matches.contains(name))
                    continue;
                try {
                    importImage(entry.path());
                    matches[name] = utf8(entry.path());
                    errors.erase(name);
                } catch (const std::exception& e) {
                    errors[name] = e.what();
                }
            }
        }
        auto result = state();
        result["importInspection"] = {{"effect", effect},    {"effects", effects}, {"name", packet.at("name")},
                                      {"missing", required}, {"folders", folders}, {"matches", matches},
                                      {"errors", errors}};
        return result;
    }
    if (op == "copy") {
        const auto effect = codec::projectEffect(document.values.at(document.selected), document.version).value;
        std::set<std::string> names, seen{document.selected}, missingEffects;
        std::vector<std::string> pending{document.selected};
        Json effects = Json::array();
        for (size_t i = 0; i < pending.size(); ++i) {
            const auto sourceName = pending[i];
            auto value = codec::projectEffect(document.values.at(sourceName), document.version).value;
            if (sourceName != document.selected)
                effects.push_back({{"name", sourceName}, {"effect", value}});
            visit(value, "", [&](Json& object, const std::string& path) {
                const auto key = textureKey(object, path);
                if (!key.empty() && object.at(key).is_string() && !object.at(key).get<std::string>().empty())
                    names.insert(object.at(key).get<std::string>());
                if (object.contains("childType") && object.contains("name") && object.at("name").is_string()) {
                    const auto child = object.at("name").get<std::string>();
                    if (!child.empty() && seen.insert(child).second) {
                        if (document.values.contains(child))
                            pending.push_back(child);
                        else
                            missingEffects.insert(child);
                    }
                }
            });
        }
        Json textures = Json::array();
        for (const auto& entry : document.textures.entries) {
            if (names.erase(entry.name))
                textures.push_back({{"name", entry.name}, {"data", entry.data}});
        }
        // PNG overrides stay preview-only in this document. A clipboard transfer
        // makes them real texture resources in the destination when pasted.
        Json missing = Json::array();
        for (const auto& name : names) {
            if (auto image = previewTextures.find(name); image != previewTextures.end())
                textures.push_back({{"name", name}, {"data", image->second}});
            else
                missing.push_back(name);
        }
        auto result = state();
        result["clipboard"] = {
            {"type", "WiiEffectEditor/effect"},     {"name", document.selected},     {"effect", effect},
            {"textures", std::move(textures)},      {"effects", std::move(effects)}, {"missingEffects", missingEffects},
            {"missingTextures", std::move(missing)}};
        return result;
    }
    if (op == "export") {
        const auto path = fs::u8path(request.at("path").get<std::string>());
        const auto packet = dispatch({{"op", "copy"}}).at("clipboard");
        if (!packet.at("missingEffects").empty())
            throw std::runtime_error("Cannot export missing child effects: " + packet.at("missingEffects").dump());
        auto effects = packet.at("effects");
        effects.insert(effects.begin(), Json{{"name", document.selected}, {"effect", packet.at("effect")}});
        std::map<std::string, std::string> names;
        std::set<std::string> filenames;
        auto filenameKey = [](std::string name) {
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return char(std::tolower(c));
            });
            return name;
        };
        for (const auto& item : effects) {
            const auto original = item.at("name").get<std::string>();
            auto name = original == document.selected ? utf8(path.stem()) : original;
            validateExportName(name);
            const auto base = name;
            unsigned suffix = 2;
            while (!filenames.insert(filenameKey(name)).second)
                name = base + "_" + std::to_string(suffix++);
            names[original] = name;
        }
        std::vector<std::pair<fs::path, Bytes>> jsonFiles;
        for (auto& item : effects) {
            auto value = item.at("effect");
            visit(value, "", [&](Json& object, const std::string&) {
                if (object.contains("childType") && object.contains("name") && object.at("name").is_string()) {
                    const auto found = names.find(object.at("name").get<std::string>());
                    if (found != names.end())
                        object["name"] = found->second;
                }
            });
            const auto text = value.dump(2) + "\n";
            const auto original = item.at("name").get<std::string>();
            const auto output =
                original == document.selected ? path : path.parent_path() / fs::u8path(names.at(original) + ".json");
            jsonFiles.emplace_back(output, Bytes(text.begin(), text.end()));
        }
        std::vector<std::pair<fs::path, Bytes>> images;
        auto folder = path.parent_path() / (path.stem().native() + fs::path("_textures").native());
        if (request.value("textures", false)) {
            if (!packet.at("missingTextures").empty())
                throw std::runtime_error(
                    "Some used textures are unavailable. Load their PNG folder or export JSON without textures.");
            std::set<std::string> filenames;
            for (const auto& texture : packet.at("textures")) {
                const auto name = texture.at("name").get<std::string>();
                if (name.empty() || name == "." || name == ".." ||
                    name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
                    std::any_of(name.begin(), name.end(), [](unsigned char c) {
                        return c < 32;
                    }))
                    throw std::runtime_error("Texture name cannot be exported as a PNG filename: " + name);
                auto key = name;
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                    return char(std::tolower(c));
                });
                if (!filenames.insert(key).second)
                    throw std::runtime_error("Texture filenames would collide: " + name);
                const auto pixels = codec::decodeTexture(texture.at("data").get<Bytes>());
                png_image image{};
                image.version = PNG_IMAGE_VERSION;
                image.width = pixels.width;
                image.height = pixels.height;
                image.format = PNG_FORMAT_RGBA;
                png_alloc_size_t size = 0;
                if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels.rgba.data(), 0, nullptr))
                    throw std::runtime_error("Could not encode PNG: " + std::string(image.message));
                Bytes bytes(size);
                if (!png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels.rgba.data(), 0, nullptr))
                    throw std::runtime_error("Could not encode PNG: " + std::string(image.message));
                bytes.resize(size);
                images.emplace_back(folder / fs::u8path(name + ".png"), std::move(bytes));
            }
        }
        // Validate and encode every image before publishing the export. JSON is
        // written last, so an image-encoding failure cannot leave a partial JSON export.
        if (!images.empty())
            fs::create_directories(folder);
        for (const auto& [image, bytes] : images)
            atomicWrite(image, bytes);
        // Publish children before the root file that refers to them.
        for (auto it = jsonFiles.rbegin(); it != jsonFiles.rend(); ++it)
            atomicWrite(it->first, it->second);
        return state();
    }
    if (op == "save") {
        auto path = effectPath, tex = texturePath;
        if (request.contains("path") && request["path"].is_string() && !request["path"].get<std::string>().empty()) {
            path = fs::absolute(fs::u8path(request["path"].get<std::string>()));
            tex = path;
            tex.replace_extension(".breft");
        }
        const unsigned version = request.value("version", document.version);
        if (version < 7 || version > 11)
            throw std::runtime_error("Choose a BREFF version from 7 through 11");
        auto candidate = document;
        candidate.version = version;
        auto effects = effectArchive(candidate).encode(), textures = textureArchive(candidate).encode();
        const bool converted = version != document.version;
        if (converted)
            publish(candidate);
        writePair(path, effects, tex, textures);
        if (converted) {
            undo.push_back(document);
            redo.clear();
        }
        // Keep the full session model and its original binary backing. Saving a
        // narrowed representation must not destroy values recoverable by switching back.
        document = std::move(candidate);
        effectPath = path;
        texturePath = tex;
        savedEffects = std::move(effects);
        savedTextures = std::move(textures);
        return state();
    }
    auto candidate = document;
    if (op == "set_version") {
        const unsigned version = request.at("version");
        if (version < 7 || version > 11)
            throw std::runtime_error("Choose a BREFF version from 7 through 11");
        if (version == document.version)
            return state();
        candidate.version = version;
    } else if (op == "replace" || op == "import" || op == "import_replace" || op == "paste") {
        Json value;
        auto attachments = request.value("effects", Json::array());
        std::string sourceName = request.value("name", document.selected);
        std::string name = document.selected;
        const bool create = op == "import" || (op == "paste" && !request.value("replace", false));
        if (op == "replace") {
            if (request.at("name") != document.selected)
                throw std::runtime_error("Selection changed");
            value = request.at("effect");
        } else if (op == "paste") {
            value = request.at("effect");
            if (create)
                name = request.at("name").get<std::string>();
        } else {
            const auto path = fs::u8path(request.at("path").get<std::string>());
            if (request.contains("effect"))
                value = request.at("effect");
            else {
                auto packet = readEffectFiles(path);
                value = packet.at("effect");
                attachments = packet.at("effects");
            }
            sourceName = utf8(path.stem());
            if (create)
                name = sourceName;
        }
        if (create) {
            const auto base = name;
            unsigned suffix = 2;
            while (
                std::any_of(candidate.effects.entries.begin(), candidate.effects.entries.end(), [&](const auto& entry) {
                    return entry.name == name;
                }))
                name = base + "_" + std::to_string(suffix++);
            validateName(name, candidate.effects);
        } else if (name.empty())
            throw std::runtime_error("Select an effect to replace");
        if (op == "replace" && document.values.contains(document.selected)) {
            const auto previous = codec::projectEffect(document.values.at(document.selected), document.version).value;
            if (value.at("animations").size() == previous.at("animations").size())
                for (size_t i = 0; i < value.at("animations").size(); ++i)
                    codec::prepareAnimationEdit(value["animations"][i], previous["animations"][i], value.at("emitter"));
            value = codec::mergeEffectEdit(document.values.at(document.selected), value, document.version);
        }
        if (op == "import" || op == "import_replace") {
            if (op == "import_replace" && request.value("target", document.selected) != document.selected)
                throw std::runtime_error("The effect selected for replacement changed");
            std::set<std::string> required;
            auto collect = [&](Json effect) {
                visit(effect, "", [&](Json& object, const std::string& path) {
                    const auto key = textureKey(object, path);
                    if (!key.empty() && object.at(key).is_string())
                        required.insert(object.at(key).get<std::string>());
                });
            };
            collect(value);
            for (const auto& child : attachments)
                collect(child.at("effect"));
            const auto textures = request.value("textureFiles", Json::object());
            for (const auto& [name, path] : textures.items()) {
                if (!required.contains(name))
                    throw std::runtime_error("Imported texture is not referenced by this effect: " + name);
                if (std::any_of(candidate.textures.entries.begin(), candidate.textures.entries.end(),
                                [&](const auto& entry) {
                                    return entry.name == name;
                                }))
                    continue;
                validateName(name, candidate.textures);
                auto bytes = importImage(fs::u8path(path.get<std::string>()));
                candidate.textures.entries.push_back({name, std::move(bytes)});
            }
        }
        if (op == "paste" || op == "import" || op == "import_replace") {
            if (request.value("replace", false) && request.value("target", document.selected) != document.selected)
                throw std::runtime_error("The effect selected for replacement changed");
            std::map<std::string, std::string> renamed;
            std::map<std::string, std::string> effectNames;
            effectNames[sourceName] = name;
            auto effects = attachments;
            if (!effects.is_array())
                throw std::runtime_error("Invalid child effect attachments");
            std::set<std::string> reserved{name};
            for (const auto& entry : candidate.effects.entries)
                reserved.insert(entry.name);
            for (const auto& child : effects) {
                const auto original = child.at("name").get<std::string>();
                if (effectNames.contains(original))
                    throw std::runtime_error("Duplicate child effect attachment");
                auto destination = original;
                unsigned suffix = 2;
                while (reserved.contains(destination))
                    destination = original + "_" + std::to_string(suffix++);
                validateName(destination, candidate.effects);
                reserved.insert(destination);
                effectNames[original] = destination;
            }
            std::set<std::string> attachedNames, destinations;
            const auto attached = request.value("textures", Json::array());
            if (!attached.is_array())
                throw std::runtime_error("Invalid clipboard textures");
            const auto choices = request.value("textureChoices", Json::object());
            for (const auto& texture : attached) {
                const std::string original = texture.at("name");
                if (!attachedNames.insert(original).second)
                    throw std::runtime_error("Duplicate attached texture name");
                auto entry = std::find_if(candidate.textures.entries.begin(), candidate.textures.entries.end(),
                                          [&](const auto& item) {
                                              return item.name == original;
                                          });
                const bool conflict = entry != candidate.textures.entries.end();
                std::string destination = original, action = "import";
                if (conflict) {
                    if (!choices.contains(original))
                        throw std::runtime_error("Choose how to resolve texture: " + original);
                    const auto& choice = choices.at(original);
                    action = choice.at("action").get<std::string>();
                    if (action == "rename")
                        destination = choice.at("name").get<std::string>();
                    else if (action != "replace" && action != "existing")
                        throw std::runtime_error("Invalid texture conflict choice");
                }
                if (!destinations.insert(destination).second)
                    throw std::runtime_error("Multiple textures would use the same destination name: " + destination);
                if (action == "existing")
                    continue;
                if (!conflict || action == "rename")
                    validateName(destination, candidate.textures);
                const auto& data = texture.at("data");
                if (!data.is_array() || data.size() > 16 * 1024 * 1024)
                    throw std::runtime_error("Invalid attached texture data");
                for (const auto& byte : data) {
                    if (!byte.is_number_integer() || byte.get<int64_t>() < 0 || byte.get<int64_t>() > 255)
                        throw std::runtime_error("Invalid attached texture byte");
                }
                auto bytes = data.get<Bytes>();
                codec::decodeTexture(bytes);
                if (action == "replace")
                    entry->data = std::move(bytes);
                else
                    candidate.textures.entries.push_back({destination, std::move(bytes)});
                if (destination != original)
                    renamed[original] = destination;
            }
            auto rewrite = [&](Json& effect) {
                visit(effect, "", [&](Json& object, const std::string& path) {
                    const auto key = textureKey(object, path);
                    if (!key.empty() && object.at(key).is_string()) {
                        const auto name = object.at(key).get<std::string>();
                        if (auto replacement = renamed.find(name); replacement != renamed.end())
                            object[key] = replacement->second;
                    }
                    if (object.contains("childType") && object.contains("name") && object.at("name").is_string()) {
                        const auto child = object.at("name").get<std::string>();
                        if (auto replacement = effectNames.find(child); replacement != effectNames.end())
                            object["name"] = replacement->second;
                    }
                });
            };
            rewrite(value);
            for (auto& child : effects) {
                auto effect = child.at("effect");
                rewrite(effect);
                const auto destination = effectNames.at(child.at("name").get<std::string>());
                candidate.values[destination] = codec::decodeEffect(codec::encodeEffect(effect, 11), 11, true);
                auto bytes = codec::encodeEffect(
                    codec::projectEffect(candidate.values[destination], candidate.effects.version).value,
                    candidate.effects.version);
                candidate.effects.entries.push_back({destination, std::move(bytes)});
            }
        }
        value = codec::decodeEffect(codec::encodeEffect(value, 11), 11, true);
        auto bytes =
            codec::encodeEffect(codec::projectEffect(value, document.effects.version).value, document.effects.version);
        if (create)
            candidate.effects.entries.push_back({name, std::move(bytes)});
        candidate.values[name] = value;
        candidate.errors.erase(name);
        candidate.selected = name;
    } else if (op == "effect_add") {
        const std::string name = request.at("name");
        validateName(name, candidate.effects);
        // The standard effect is compiled into the application.
        const Json standard = Json::parse(
#include "standard_effect.inc"
        );
        candidate.values[name] = codec::projectEffect(standard, candidate.version).value;
        auto bytes = codec::encodeEffect(codec::projectEffect(candidate.values[name], candidate.effects.version).value,
                                         candidate.effects.version);
        candidate.effects.entries.push_back({name, std::move(bytes)});
        candidate.selected = name;
    } else if (op == "effect_rename") {
        const std::string name = request.at("name");
        if (name == document.selected)
            return state();
        validateName(name, candidate.effects);
        for (auto& entry : candidate.effects.entries)
            if (entry.name == document.selected)
                entry.name = name;
        if (candidate.values.contains(document.selected)) {
            candidate.values[name] = candidate.values.at(document.selected);
            candidate.values.erase(document.selected);
        }
        if (candidate.originalValues.contains(document.selected)) {
            candidate.originalValues[name] = candidate.originalValues.at(document.selected);
            candidate.originalValues.erase(document.selected);
        }
        if (candidate.errors.contains(document.selected)) {
            candidate.errors[name] = candidate.errors.at(document.selected);
            candidate.errors.erase(document.selected);
        }
        for (auto& value : candidate.values)
            visit(value, "", [&](Json& object, const std::string&) {
                if (object.contains("childType") && object.value("name", std::string()) == document.selected)
                    object["name"] = name;
            });
        candidate.selected = name;
    } else if (op == "effect_delete") {
        std::erase_if(candidate.effects.entries, [&](const auto& e) {
            return e.name == document.selected;
        });
        candidate.values.erase(document.selected);
        candidate.errors.erase(document.selected);
        candidate.selected = candidate.effects.entries.empty() ? "" : candidate.effects.entries.front().name;
    } else if (op.starts_with("texture_")) {
        const std::string name = request.at("name");
        auto& entries = candidate.textures.entries;
        auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
            return e.name == name;
        });
        if (op == "texture_add") {
            validateName(name, candidate.textures);
            entries.push_back({name, importImage(fs::u8path(request.at("path").get<std::string>()))});
        } else {
            if (entry == entries.end())
                throw std::runtime_error("Unknown texture");
            if (op == "texture_replace")
                entry->data = importImage(fs::u8path(request.at("path").get<std::string>()));
            else {
                const std::string replacement = op == "texture_rename" ? request.at("newName").get<std::string>()
                                                                       : request.value("replacement", std::string());
                if (op == "texture_rename") {
                    if (replacement == name)
                        return state();
                    validateName(replacement, candidate.textures);
                    entry->name = replacement;
                } else if (op == "texture_delete") {
                    if (!replacement.empty() &&
                        (replacement == name || std::none_of(entries.begin(), entries.end(), [&](const auto& e) {
                             return e.name == replacement;
                         })))
                        throw std::runtime_error("Choose an available replacement texture");
                    entries.erase(entry);
                } else
                    throw std::runtime_error("Unknown texture operation");
                for (auto& effect : candidate.values)
                    visit(effect, "", [&](Json& object, const std::string& path) {
                        auto key = textureKey(object, path);
                        if (!key.empty() && object[key] == name) {
                            object[key] = replacement;
                        }
                    });
            }
        }
    } else
        throw std::runtime_error("Unknown editor operation: " + op);
    publish(candidate);
    undo.push_back(document);
    if (undo.size() > 100)
        undo.erase(undo.begin());
    redo.clear();
    document = std::move(candidate);
    return state();
}
Json DocumentService::handle(const Json& request) {
    try {
        return {{"ok", true}, {"data", dispatch(request)}};
    } catch (const std::exception& e) {
        return {{"ok", false}, {"error", e.what()}};
    }
}

Workspace::Json Workspace::state() const {
    Json result = {{"loaded", false}};
    Json files = Json::array();
    for (const auto& tab : tabs) {
        if (tab.id == selected)
            result = tab.state;
        files.push_back({{"id", tab.id}, {"path", tab.state.at("path")}, {"dirty", tab.state.value("dirty", false)}});
    }
    result["documents"] = std::move(files);
    result["documentId"] = selected;
    return result;
}

Workspace::Json Workspace::handle(const Json& request) {
    try {
        const std::string op = request.at("op");
        Json clipboard, importInspection;
        auto current = std::find_if(tabs.begin(), tabs.end(), [&](const auto& tab) {
            return tab.id == selected;
        });
        if (op == "open") {
            const auto path = fs::weakly_canonical(fs::u8path(request.at("breff").get<std::string>()));
            auto texture = path;
            texture.replace_extension(".breft");
            if (request.contains("breft") && request["breft"].is_string() &&
                !request["breft"].get<std::string>().empty())
                texture = fs::weakly_canonical(fs::u8path(request["breft"].get<std::string>()));
            auto existing = std::find_if(tabs.begin(), tabs.end(), [&](const auto& tab) {
                return fs::weakly_canonical(fs::u8path(tab.state.at("path").template get<std::string>())) == path &&
                       fs::weakly_canonical(
                           fs::u8path(tab.state.at("textureSourcePath").template get<std::string>())) == texture;
            });
            if (existing != tabs.end())
                selected = existing->id;
            else {
                auto document = std::make_unique<DocumentService>();
                auto response = document->handle(request);
                if (!response.value("ok", false))
                    return response;
                selected = nextId++;
                tabs.push_back({selected, std::move(document), response.at("data")});
            }
        } else if (op == "document_select" || op == "document_close") {
            const uint64_t id = request.at("documentId");
            auto tab = std::find_if(tabs.begin(), tabs.end(), [&](const auto& item) {
                return item.id == id;
            });
            if (tab == tabs.end())
                throw std::runtime_error("The file is no longer open");
            if (op == "document_select")
                selected = id;
            else {
                if (tab->state.value("dirty", false) && !request.value("discard", false))
                    throw std::runtime_error("Save or discard this file's changes before closing");
                const auto index = std::distance(tabs.begin(), tab);
                tabs.erase(tab);
                if (selected == id)
                    selected = tabs.empty() ? 0 : tabs[std::min(size_t(index), tabs.size() - 1)].id;
            }
        } else if (op == "document_save") {
            const uint64_t id = request.at("documentId");
            auto tab = std::find_if(tabs.begin(), tabs.end(), [&](const auto& item) {
                return item.id == id;
            });
            if (tab == tabs.end())
                throw std::runtime_error("The file is no longer open");
            auto response = tab->document->handle(
                {{"op", "save"}, {"version", request.value("version", tab->state.at("version").get<unsigned>())}});
            if (!response.value("ok", false))
                return response;
            tab->state = response.at("data");
        } else if (op == "save_all") {
            for (auto& tab : tabs) {
                const auto versions = request.value("versions", Json::object());
                auto response = tab.document->handle(
                    {{"op", "save"},
                     {"version", versions.value(std::to_string(tab.id), tab.state.at("version").get<unsigned>())}});
                if (!response.value("ok", false))
                    return response;
                tab.state = response.at("data");
            }
        } else if (op != "state") {
            if (current == tabs.end())
                throw std::runtime_error("Open a BREFF/BREFT pair first");
            // A Save As must not overwrite another document that is open here.
            if (op == "save" && request.contains("path") && request["path"].is_string() &&
                !request["path"].get<std::string>().empty()) {
                const auto path = fs::weakly_canonical(fs::u8path(request["path"].get<std::string>()));
                auto texture = path;
                texture.replace_extension(".breft");
                for (const auto& tab : tabs)
                    if (tab.id != selected &&
                        (fs::weakly_canonical(fs::u8path(tab.state.at("path").template get<std::string>())) == path ||
                         fs::weakly_canonical(
                             fs::u8path(tab.state.at("textureSourcePath").template get<std::string>())) == texture))
                        throw std::runtime_error("That file is already open in another tab");
            }
            if ((op == "paste" || op == "import" || op == "import_replace" || op == "import_scan") &&
                request.value("documentId", selected) != selected)
                throw std::runtime_error("The destination file changed; paste again in the intended tab");
            auto response = current->document->handle(request);
            if (!response.value("ok", false))
                return response;
            current->state = response.at("data");
            if (current->state.contains("clipboard")) {
                clipboard = std::move(current->state["clipboard"]);
                current->state.erase("clipboard");
            }
            if (current->state.contains("importInspection")) {
                importInspection = std::move(current->state["importInspection"]);
                current->state.erase("importInspection");
            }
        }
        auto result = state();
        if (!clipboard.is_null())
            result["clipboard"] = std::move(clipboard);
        if (!importInspection.is_null())
            result["importInspection"] = std::move(importInspection);
        return {{"ok", true}, {"data", std::move(result)}};
    } catch (const std::exception& e) {
        return {{"ok", false}, {"error", e.what()}};
    }
}

}
