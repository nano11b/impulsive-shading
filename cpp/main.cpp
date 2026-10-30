#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <embedded_shaders.h>
#include <embedded_previews.h>
#include "audio.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#include <numeric>

namespace fs = std::filesystem;
using json = nlohmann::json;
static constexpr const char* kAppName = "Impulsive Shading";

static json frameStatistics(std::vector<double> frameMs) {
    if (frameMs.empty()) throw std::runtime_error("Benchmark recorded no frames");
    const double totalMs = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);
    std::sort(frameMs.begin(), frameMs.end());
    const auto percentile = [&](double fraction) {
        const double position = fraction * static_cast<double>(frameMs.size() - 1);
        const size_t lower = static_cast<size_t>(position);
        const size_t upper = std::min(lower + 1, frameMs.size() - 1);
        return frameMs[lower] + (frameMs[upper] - frameMs[lower]) * (position - lower);
    };
    return {{"frames", frameMs.size()}, {"elapsed_seconds", totalMs / 1000.0},
            {"average_fps", 1000.0 * frameMs.size() / totalMs},
            {"mean_frame_ms", totalMs / frameMs.size()},
            {"median_frame_ms", percentile(0.5)}, {"p95_frame_ms", percentile(0.95)},
            {"p99_frame_ms", percentile(0.99)}};
}

struct Input {
    int channel = 0;
    std::string type, id, filepath, filter = "linear", wrap = "repeat";
    GLuint texture = 0;
    int width = 1, height = 1;
    int videoFrame = -1;
};
struct MediaBlob { const unsigned char* data = nullptr; size_t size = 0; };
static MediaBlob embeddedMedia(int id) {
#ifdef _WIN32
    HRSRC resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource) return {};
    HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
    return {static_cast<const unsigned char*>(LockResource(loaded)), SizeofResource(GetModuleHandleW(nullptr), resource)};
#else
    if (id != 101 && id != 102) return {};
    static std::vector<unsigned char> frames, soundtrack;
    static bool framesLoaded = false, soundtrackLoaded = false;
    auto& bytes = id == 101 ? frames : soundtrack;
    auto& attempted = id == 101 ? framesLoaded : soundtrackLoaded;
    if (!attempted) {
        attempted = true;
        try {
            const fs::path executable = fs::read_symlink("/proc/self/exe");
            const fs::path path = executable.parent_path() / "data" /
                (id == 101 ? "logo_spin_frames.bin" : "logo_spin_audio.mp3");
            std::ifstream file(path, std::ios::binary);
            if (file)
                bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        } catch (const fs::filesystem_error&) {}
    }
    return {bytes.empty() ? nullptr : bytes.data(), bytes.size()};
#endif
}
struct VideoFrames {
    MediaBlob blob;
    unsigned width = 0, height = 0, fpsNumerator = 0, fpsDenominator = 0, count = 0;
    size_t payload = 0;
    std::vector<uint32_t> offsets;
    static uint32_t word(const unsigned char* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    explicit VideoFrames(MediaBlob media) : blob(media) {
        if (!blob.data || blob.size < 28 || std::string(reinterpret_cast<const char*>(blob.data), 4) != "LSF1") return;
        width = word(blob.data + 4); height = word(blob.data + 8);
        fpsNumerator = word(blob.data + 12); fpsDenominator = word(blob.data + 16);
        count = word(blob.data + 20);
        if (!width || !height || !fpsNumerator || !fpsDenominator || !count || count > 100000 ||
            size_t(count + 1) * 4 > blob.size - 24) { count = 0; return; }
        payload = 24 + size_t(count + 1) * 4;
        for (unsigned i = 0; i <= count; ++i) offsets.push_back(word(blob.data + 24 + i * 4));
        if (offsets.front() != 0 || offsets.back() > blob.size - payload ||
            !std::is_sorted(offsets.begin(), offsets.end())) count = 0;
    }
    MediaBlob frame(unsigned index) const {
        if (!count || index >= count) return {};
        return {blob.data + payload + offsets[index], offsets[index + 1] - offsets[index]};
    }
};
struct Target {
    GLuint texture = 0, framebuffer = 0;
};
struct Pass {
    std::string name, type, id, code;
    std::vector<Input> inputs;
    GLuint program = 0;
    Target targets[2];
    int front = 0;
};
struct Shader {
    std::string name, id, common;
    std::vector<Pass> passes;
    GLuint preview = 0;
};

struct Settings {
    bool fullscreen = false;
    bool vsync = true;
    int width = 1280, height = 720;
    float timeSpeed = 1.0f;
    std::string monitor, shaderId;
};

static fs::path settingsPath() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "Impulsive Shading" / "settings.json";
#endif
    if (const char* home = std::getenv("HOME"))
        return fs::path(home) / ".config" / "Impulsive Shading" / "settings.json";
    return fs::current_path() / "settings.json";
}

static Settings loadSettings() {
    Settings settings;
    std::ifstream file(settingsPath());
    if (!file) return settings;
    try {
        const auto data = json::parse(file);
        settings.fullscreen = data.value("fullscreen", false);
        settings.vsync = data.value("vsync", true);
        settings.width = std::clamp(data.value("width", 1280), 640, 7680);
        settings.height = std::clamp(data.value("height", 720), 480, 4320);
        settings.timeSpeed = std::clamp(data.value("timeSpeed", 1.0f), 0.1f, 3.0f);
        settings.monitor = data.value("monitor", "");
        settings.shaderId = data.value("shaderId", "");
    } catch (const std::exception& error) {
        std::cerr << "Ignoring invalid settings: " << error.what() << '\n';
    }
    return settings;
}

static void saveSettings(const Settings& settings) {
    try {
        const auto path = settingsPath();
        fs::create_directories(path.parent_path());
        std::ofstream file(path);
        file << json{{"fullscreen", settings.fullscreen}, {"vsync", settings.vsync}, {"width", settings.width},
                     {"height", settings.height}, {"timeSpeed", settings.timeSpeed},
                     {"monitor", settings.monitor}, {"shaderId", settings.shaderId}}.dump(2);
    } catch (const std::exception& error) {
        std::cerr << "Could not save settings: " << error.what() << '\n';
    }
}

static const char* vertexSource = R"GLSL(#version 330 core
const vec2 vertices[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main() { gl_Position = vec4(vertices[gl_VertexID], 0, 1); }
)GLSL";

static std::string fragmentSource(const Shader& shader, const Pass& pass) {
    auto clean = [](const std::string& source) {
        std::string result, line;
        std::istringstream stream(source);
        while (std::getline(stream, line)) {
            const auto first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line.compare(first, 8, "#version") == 0) continue;
            result += line + '\n';
        }
        return result;
    };
    const std::string entry = pass.type == "sound"
        ? "\nuniform int _soundSampleOffset;\nvoid main() { int sample = _soundSampleOffset + int(gl_FragCoord.x); _color = vec4(mainSound(sample, float(sample) / iSampleRate), 0.0, 1.0); }\n"
        : "\nvoid main() { mainImage(_color, gl_FragCoord.xy); }\n";
    return std::string(R"GLSL(#version 330 core
out vec4 _color;
uniform vec3 iResolution;
uniform float iTime, iTimeDelta, iFrameRate, iSampleRate;
uniform int iFrame;
uniform vec4 iMouse, iDate;
uniform float iChannelTime[4];
uniform vec3 iChannelResolution[4];
uniform sampler2D iChannel0, iChannel1, iChannel2, iChannel3;
#define texture2D texture
#define textureCube texture
)GLSL") + clean(shader.common) + "\n" + clean(pass.code) + entry;
}

static GLuint compile(GLenum kind, const std::string& source, const std::string& label) {
    GLuint shader = glCreateShader(kind);
    const char* pointer = source.c_str();
    glShaderSource(shader, 1, &pointer, nullptr);
    glCompileShader(shader);
    GLint okay = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(label + " compilation failed:\n" + log);
    }
    return shader;
}

static GLuint buildProgram(const Shader& shader, const Pass& pass) {
    GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, "Vertex shader");
    GLuint fragment = 0, program = 0;
    try {
        fragment = compile(GL_FRAGMENT_SHADER, fragmentSource(shader, pass), shader.name + " / " + pass.name);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint okay = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &okay);
        if (!okay) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error(shader.name + " / " + pass.name + " link failed:\n" + log);
        }
    } catch (...) {
        if (program) glDeleteProgram(program);
        if (fragment) glDeleteShader(fragment);
        glDeleteShader(vertex);
        throw;
    }
    glDeleteShader(fragment);
    glDeleteShader(vertex);
    return program;
}

static std::vector<Shader> loadJson(const json& root) {
    std::vector<Shader> shaders;
    for (const auto& item : root.at("shaders")) {
        Shader shader;
        shader.name = item.at("info").value("name", "Untitled");
        shader.id = item.at("info").value("id", "");
        for (const auto& source : item.at("renderpass")) {
            const std::string type = source.value("type", "");
            if (type == "common") {
                shader.common += source.value("code", "") + "\n";
                continue;
            }
            if (type != "image" && type != "buffer" && type != "sound") continue;
            Pass pass;
            pass.name = source.value("name", type);
            pass.type = type;
            pass.code = source.value("code", "");
            if (source.contains("outputs")) {
                for (const auto& output : source.at("outputs")) {
                    if (output.contains("id")) { pass.id = output.at("id").get<std::string>(); break; }
                }
            }
            if (source.contains("inputs")) {
                for (const auto& channel : source.at("inputs")) {
                    Input input;
                    input.channel = channel.value("channel", 0);
                    input.type = channel.value("type", "");
                    input.id = channel.value("id", "");
                    input.filepath = channel.value("filepath", "");
                    if (channel.contains("sampler")) {
                        input.filter = channel.at("sampler").value("filter", "linear");
                        input.wrap = channel.at("sampler").value("wrap", "repeat");
                    }
                    pass.inputs.push_back(std::move(input));
                }
            }
            shader.passes.push_back(std::move(pass));
        }
        shaders.push_back(std::move(shader));
    }
    return shaders;
}

static GLuint makeTexture(int width, int height, const unsigned char* pixels, GLenum format = GL_RGBA8) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return texture;
}

static GLuint makeSubstituteTexture(const std::string& key, int& width, int& height) {
    width = height = 256;
    unsigned int seed = 2166136261u;
    for (unsigned char value : key) seed = (seed ^ value) * 16777619u;
    std::vector<unsigned char> pixels(static_cast<size_t>(width * height * 4));
    auto hash = [seed](int x, int y) {
        unsigned int value = seed ^ (static_cast<unsigned int>(x) * 374761393u) ^
            (static_cast<unsigned int>(y) * 668265263u);
        value = (value ^ (value >> 13)) * 1274126177u;
        return static_cast<float>(value & 255u) / 255.0f;
    };
    const float tintR = 0.35f + 0.65f * hash(11, 19);
    const float tintG = 0.35f + 0.65f * hash(23, 7);
    const float tintB = 0.35f + 0.65f * hash(3, 31);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int cellX = x / 16, cellY = y / 16;
        const float fx = static_cast<float>(x % 16) / 16.0f;
        const float fy = static_cast<float>(y % 16) / 16.0f;
        const float sx = fx * fx * (3.0f - 2.0f * fx);
        const float sy = fy * fy * (3.0f - 2.0f * fy);
        const float n0 = hash(cellX, cellY) * (1 - sx) + hash(cellX + 1, cellY) * sx;
        const float n1 = hash(cellX, cellY + 1) * (1 - sx) + hash(cellX + 1, cellY + 1) * sx;
        const float noise = (n0 * (1 - sy) + n1 * sy) * 0.75f + hash(x, y) * 0.25f;
        const size_t offset = static_cast<size_t>((y * width + x) * 4);
        pixels[offset] = static_cast<unsigned char>(std::clamp(noise * tintR * 255.0f, 0.0f, 255.0f));
        pixels[offset + 1] = static_cast<unsigned char>(std::clamp(noise * tintG * 255.0f, 0.0f, 255.0f));
        pixels[offset + 2] = static_cast<unsigned char>(std::clamp(noise * tintB * 255.0f, 0.0f, 255.0f));
        pixels[offset + 3] = 255;
    }
    return makeTexture(width, height, pixels.data());
}

static GLuint makeAudioTexture() {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, AudioEngine::width, AudioEngine::height,
        0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

static void destroyTarget(Target& target) {
    if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
    if (target.texture) glDeleteTextures(1, &target.texture);
    target = {};
}

static void makeTarget(Target& target, int width, int height) {
    destroyTarget(target);
    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Could not create a buffer framebuffer");
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

class App {
public:
    App(GLFWwindow* window, std::vector<Shader> shaders, fs::path assets, bool uiEnabled,
        bool persistSettings, Settings settings)
        : window_(window), shaders_(std::move(shaders)), assets_(std::move(assets)),
          settings_(std::move(settings)), uiEnabled_(uiEnabled), persistSettings_(persistSettings),
          fullscreen_(settings_.fullscreen), vsync_(settings_.vsync),
          timeSpeed_(settings_.timeSpeed), video_(embeddedMedia(101)) {
        glfwGetWindowPos(window_, &windowX_, &windowY_);
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        const unsigned char black[4] = {0, 0, 0, 255};
        black_ = makeTexture(1, 1, black);
        keyboardPixels_.fill(0);
        keyboard_ = makeTexture(256, 3, keyboardPixels_.data());
        musicTexture_ = makeAudioTexture();
        makeTarget(soundTarget_, AudioEngine::soundBlockFrames, 1);
        for (auto& shader : shaders_) {
            const std::string bytes = embeddedPreview(shader.id);
            if (bytes.empty()) continue;
            int width = 0, height = 0, channels = 0;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                &width, &height, &channels, 4);
            if (!pixels) continue;
            shader.preview = makeTexture(width, height, pixels);
            stbi_image_free(pixels);
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) {
            auto& app = *static_cast<App*>(glfwGetWindowUserPointer(w));
            app.mouseX_ = x; app.mouseY_ = y;
        });
        glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int) {
            if (button != GLFW_MOUSE_BUTTON_LEFT) return;
            auto& app = *static_cast<App*>(glfwGetWindowUserPointer(w));
            app.mouseDown_ = action == GLFW_PRESS;
            if (app.mouseDown_) { app.clickX_ = app.mouseX_; app.clickY_ = app.mouseY_; }
        });
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int) {
            auto& app = *static_cast<App*>(glfwGetWindowUserPointer(w));
            if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
                if (app.uiEnabled_) app.menuVisible_ = !app.menuVisible_;
                else glfwSetWindowShouldClose(w, GLFW_TRUE);
            }
            if (action == GLFW_PRESS && key == GLFW_KEY_M && app.uiEnabled_) app.menuVisible_ = !app.menuVisible_;
            if (action == GLFW_PRESS && key == GLFW_KEY_F12 && app.uiEnabled_) app.captureRequested_ = true;
            if (!app.menuVisible_) {
                if (action == GLFW_PRESS && key == GLFW_KEY_RIGHT) app.select((app.selected_ + 1) % app.shaders_.size());
                if (action == GLFW_PRESS && key == GLFW_KEY_LEFT) app.select((app.selected_ + app.shaders_.size() - 1) % app.shaders_.size());
                if (action == GLFW_PRESS && key == GLFW_KEY_R) app.reload();
                if (action == GLFW_PRESS && key == GLFW_KEY_P) app.paused_ = !app.paused_;
            }
            if (key >= 0 && key < 256) {
                const size_t k = static_cast<size_t>(key) * 4;
                app.keyboardPixels_[k] = action == GLFW_RELEASE ? 0 : 255;
                if (action == GLFW_PRESS) {
                    app.keyboardPixels_[256 * 4 + k] = 255;
                    app.keyboardPixels_[512 * 4 + k] ^= 255;
                }
            }
        });
        if (uiEnabled_) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::StyleColorsDark();
            ImGui::GetStyle().WindowRounding = 12;
            ImGui::GetStyle().FrameRounding = 6;
            ImGui_ImplGlfw_InitForOpenGL(window_, true);
            ImGui_ImplOpenGL3_Init("#version 330 core");
            buildMonitors();
            buildResolutions();
        }
    }

    ~App() {
        if (persistSettings_) {
            settings_.timeSpeed = timeSpeed_;
            saveSettings(settings_);
        }
        if (uiEnabled_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        for (auto& shader : shaders_) {
            if (shader.preview) glDeleteTextures(1, &shader.preview);
            for (auto& pass : shader.passes) {
                if (pass.program) glDeleteProgram(pass.program);
                for (auto& target : pass.targets) destroyTarget(target);
                for (auto& input : pass.inputs) if (input.texture) glDeleteTextures(1, &input.texture);
            }
        }
        destroyTarget(imageTarget_);
        destroyTarget(soundTarget_);
        glDeleteTextures(1, &keyboard_);
        glDeleteTextures(1, &black_);
        glDeleteTextures(1, &musicTexture_);
        glDeleteVertexArrays(1, &vao_);
    }

    void restoreDisplay() { if (uiEnabled_) applyDisplay(); }
    bool hasSoundPass() const {
        const auto& passes = shaders_[selected_].passes;
        return std::any_of(passes.begin(), passes.end(),
            [](const Pass& pass) { return pass.type == "sound"; });
    }
    float validateSound() {
        nextSoundSample_ = 88200;
        float energy = 0.0f;
        for (const auto& pass : shaders_[selected_].passes) if (pass.type == "sound" && pass.program) {
            for (int i = 0; i < 4; ++i) energy += renderSoundBlock(pass);
            break;
        }
        return energy * 0.25f;
    }
    bool validateAudioDevice() {
        audio_.setSoundMode(true);
        std::array<float, AudioEngine::soundBlockFrames * 4> silence{};
        audio_.enqueueSound(silence.data(), AudioEngine::soundBlockFrames);
        const bool started = audio_.setOutput(true);
        if (started) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        audio_.setOutput(false);
        return started;
    }
    void setPreviewTime(float time) { shaderTime_ = time; }
    void setPausedForValidation(bool paused) { paused_ = paused; }
    void setTimeSpeedForValidation(float speed) { timeSpeed_ = speed; }
    float shaderTime() const { return shaderTime_; }
    int frameCount() const { return frame_; }
    bool savePreview(const fs::path& path) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, imageTarget_.framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<unsigned char> pixels(static_cast<size_t>(width_) * height_ * 4);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        fs::create_directories(path.parent_path());
        return stbi_write_png(path.string().c_str(), width_, height_, 4, pixels.data(), width_ * 4) != 0;
    }
    bool saveUiFrame(const fs::path& path) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<unsigned char> pixels(static_cast<size_t>(width_) * height_ * 4);
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(path.string().c_str(), width_, height_, 4, pixels.data(), width_ * 4) != 0;
    }

    bool select(size_t index) {
        const size_t previous = selected_;
        selected_ = index;
        try {
            auto& shader = shaders_.at(selected_);
            for (auto& pass : shader.passes) {
                if (!pass.program) pass.program = buildProgram(shader, pass);
                for (auto& input : pass.inputs) loadImage(input);
            }
            if (previous != selected_) {
                for (auto& pass : shaders_[previous].passes)
                    for (auto& target : pass.targets) destroyTarget(target);
            }
            resetBuffers();
            audio_.setOutput(false);
            audio_.clearMusic();
            frame_ = 0; shaderTime_ = 0; paused_ = false; audio_.seek(0);
            nextSoundSample_ = 0;
            const bool hasSound = std::any_of(shader.passes.begin(), shader.passes.end(),
                [](const Pass& pass) { return pass.type == "sound"; });
            bool hasMusicInput = false;
            for (const auto& pass : shader.passes) for (const auto& input : pass.inputs)
                if (input.type == "music") {
                    hasMusicInput = true;
                    if (input.filepath == "embedded:logo-spin-audio") {
                        const auto media = embeddedMedia(102);
                        if (!audio_.loadMusicMemory(media.data, media.size))
                            std::cerr << "Could not decode embedded logo audio\n";
                    } else {
                        auto path = assets_ / fs::path(input.filepath).relative_path();
                        if (!fs::exists(path)) path = assets_ / fs::path(input.filepath).filename();
                        audio_.loadMusic(path);
                    }
                }
            audio_.setSoundMode(hasSound);
            musicOutput_ = uiEnabled_ && (hasSound || hasMusicInput);
            if (!audio_.setOutput(musicOutput_)) {
                musicOutput_ = false;
                std::cerr << "Audio output unavailable\n";
            }
            last_ = glfwGetTime();
            glfwSetWindowTitle(window_, (std::string(kAppName) + " - " + shader.name + " [" + shader.id + "]").c_str());
            if (persistSettings_) { settings_.shaderId = shader.id; saveSettings(settings_); }
            std::cout << "Loaded " << shader.name << " [" << shader.id << "]\n";
            return true;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            selected_ = previous;
            return false;
        }
    }

    void reload() {
        auto& shader = shaders_.at(selected_);
        try {
            for (auto& pass : shader.passes) {
                GLuint replacement = buildProgram(shader, pass);
                if (pass.program) glDeleteProgram(pass.program);
                pass.program = replacement;
            }
            resetBuffers(); frame_ = 0; shaderTime_ = 0; paused_ = false; audio_.seek(0);
            nextSoundSample_ = 0; last_ = glfwGetTime();
            std::cout << "Recompiled " << shader.name << '\n';
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    }

    void render() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (width <= 0 || height <= 0) return;
        if (width != width_ || height != height_) { width_ = width; height_ = height; resetBuffers(); frame_ = 0; }
        const double now = glfwGetTime();
        const float wallDelta = frame_ ? static_cast<float>(now - last_) : 0.0f;
        last_ = now;
        if (wallDelta > 0 && wallDelta < 1) fps_ = fps_ == 0 ? 1.0f / wallDelta : fps_ * 0.9f + (1.0f / wallDelta) * 0.1f;
        const float delta = paused_ ? 0.0f : wallDelta * timeSpeed_;
        shaderTime_ += delta;
        const float time = shaderTime_;
        audio_.setSpeed(timeSpeed_);
        audio_.setPaused(paused_);
        if (!paused_) {
        audio_.update(time);
        glBindTexture(GL_TEXTURE_2D, musicTexture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, AudioEngine::width, AudioEngine::height,
            GL_RED, GL_FLOAT, audio_.musicPixels());
        glBindTexture(GL_TEXTURE_2D, keyboard_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 3, GL_RGBA, GL_UNSIGNED_BYTE, keyboardPixels_.data());
        auto& shader = shaders_[selected_];
        if (musicOutput_ && audio_.soundMode()) {
            for (auto& pass : shader.passes) if (pass.type == "sound" && pass.program) {
                for (int block = 0; block < 4 && audio_.queuedSoundFrames() < 8192; ++block)
                    renderSoundBlock(pass);
                break;
            }
        }
        // ShaderToy evaluates buffers A-D, then Image, regardless of JSON pass order.
        for (int stage = 0; stage < 5; ++stage) {
            const std::string wanted = stage == 4 ? "Image" : "Buffer " + std::string(1, static_cast<char>('A' + stage));
            for (auto& pass : shader.passes) {
                if (pass.name != wanted || !pass.program) continue;
                const bool buffer = pass.type == "buffer";
                if (buffer) glBindFramebuffer(GL_FRAMEBUFFER, pass.targets[1 - pass.front].framebuffer);
                else glBindFramebuffer(GL_FRAMEBUFFER, imageTarget_.framebuffer);
                glViewport(0, 0, width_, height_);
                glUseProgram(pass.program);
                uniforms(pass.program, time, delta);
                for (int channel = 0; channel < 4; ++channel) {
                    GLuint texture = black_;
                    int tw = 1, th = 1;
                    Input* source = nullptr;
                    for (auto& input : pass.inputs) if (input.channel == channel) { source = &input; break; }
                    if (source) {
                        if (source->type == "video") updateVideo(*source, time);
                        if (source->type == "buffer") {
                            for (const auto& producer : shader.passes) if (producer.type == "buffer" && producer.id == source->id) {
                                texture = producer.targets[producer.front].texture;
                                tw = width_; th = height_;
                                break;
                            }
                        } else if (source->type == "keyboard") {
                            texture = keyboard_; tw = 256; th = 3;
                        } else if (source->type == "music" || source->type == "mic") {
                            texture = musicTexture_;
                            tw = AudioEngine::width; th = AudioEngine::height;
                        } else if (source->texture) {
                            texture = source->texture; tw = source->width; th = source->height;
                        }
                    }
                    glActiveTexture(GL_TEXTURE0 + channel);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    if (source) {
                        const bool nearest = source->filter == "nearest";
                        const bool clamp = source->wrap == "clamp";
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                    }
                    uniform1i(pass.program, "iChannel" + std::to_string(channel), channel);
                    const auto resolution = glGetUniformLocation(pass.program, ("iChannelResolution[" + std::to_string(channel) + "]").c_str());
                    if (resolution >= 0) glUniform3f(resolution, static_cast<float>(tw), static_cast<float>(th), 1);
                    const auto channelTime = glGetUniformLocation(pass.program, ("iChannelTime[" + std::to_string(channel) + "]").c_str());
                    if (channelTime >= 0) glUniform1f(channelTime, time);
                }
                glBindVertexArray(vao_);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                if (buffer) pass.front = 1 - pass.front;
            }
        }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, imageTarget_.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (captureRequested_) { captureScreenshot(); captureRequested_ = false; }
        if (uiEnabled_) drawUi();
        glfwSwapBuffers(window_);
        if (!paused_) {
            std::fill(keyboardPixels_.begin() + 256 * 4, keyboardPixels_.begin() + 512 * 4, static_cast<unsigned char>(0));
            ++frame_;
        }
    }

private:
    struct Resolution { int width, height, refresh; bool native; };
    struct Monitor { GLFWmonitor* handle; std::string name; };

    float renderSoundBlock(const Pass& pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, soundTarget_.framebuffer);
        glViewport(0, 0, AudioEngine::soundBlockFrames, 1);
        glUseProgram(pass.program);
        uniforms(pass.program, static_cast<float>(nextSoundSample_) / 44100.0f, 0.0f);
        uniform1i(pass.program, "_soundSampleOffset", static_cast<int>(nextSoundSample_));
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<float, AudioEngine::soundBlockFrames * 4> pixels{};
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, AudioEngine::soundBlockFrames, 1, GL_RGBA, GL_FLOAT, pixels.data());
        audio_.enqueueSound(pixels.data(), AudioEngine::soundBlockFrames);
        nextSoundSample_ += AudioEngine::soundBlockFrames;
        float energy = 0.0f;
        for (int i = 0; i < AudioEngine::soundBlockFrames; ++i)
            if (std::isfinite(pixels[static_cast<size_t>(i * 4)]))
                energy += std::abs(pixels[static_cast<size_t>(i * 4)]);
        return energy / AudioEngine::soundBlockFrames;
    }

    void captureScreenshot() {
        try {
            fs::path base;
#ifdef _WIN32
            if (const char* profile = std::getenv("USERPROFILE")) base = fs::path(profile) / "Pictures";
#endif
            if (base.empty()) base = fs::current_path();
            base /= "Impulsive Shading";
            fs::create_directories(base);
            const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm calendar{};
#ifdef _WIN32
            localtime_s(&calendar, &stamp);
#else
            localtime_r(&stamp, &calendar);
#endif
            std::ostringstream name;
            name << "shader-" << shaders_[selected_].id << '-'
                 << std::put_time(&calendar, "%Y%m%d-%H%M%S") << '-' << frame_ << ".png";
            const auto path = base / name.str();
            if (!savePreview(path))
                throw std::runtime_error("PNG encoding failed");
            std::cout << "Saved screenshot: " << path.string() << '\n';
        } catch (const std::exception& error) {
            std::cerr << "Screenshot failed: " << error.what() << '\n';
        }
    }

    void buildMonitors() {
        monitors_.clear();
        int count = 0;
        GLFWmonitor** handles = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            const char* name = glfwGetMonitorName(handles[i]);
            monitors_.push_back({handles[i], name ? name : "Display"});
            if (monitors_.back().name == settings_.monitor) selectedMonitor_ = i;
        }
        if (monitors_.empty()) throw std::runtime_error("No display monitor found");
    }

    void buildResolutions() {
        resolutions_.clear();
        const std::array<std::pair<int, int>, 5> common = {{{800, 600}, {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}}};
        for (auto [width, height] : common) resolutions_.push_back({width, height, GLFW_DONT_CARE, false});
        int count = 0;
        const GLFWvidmode* modes = glfwGetVideoModes(monitors_[static_cast<size_t>(selectedMonitor_)].handle, &count);
        for (int i = 0; i < count; ++i) {
            if (modes[i].width < 640 || modes[i].height < 480) continue;
            auto existing = std::find_if(resolutions_.begin(), resolutions_.end(), [&](const Resolution& resolution) {
                return resolution.width == modes[i].width && resolution.height == modes[i].height;
            });
            if (existing == resolutions_.end()) resolutions_.push_back({modes[i].width, modes[i].height, modes[i].refreshRate, true});
            else { existing->refresh = modes[i].refreshRate; existing->native = true; }
        }
        std::sort(resolutions_.begin(), resolutions_.end(), [](const Resolution& a, const Resolution& b) {
            return a.width * a.height < b.width * b.height ||
                (a.width * a.height == b.width * b.height && a.width < b.width);
        });
        bool matched = false;
        for (size_t i = 0; i < resolutions_.size(); ++i)
            if (resolutions_[i].width == settings_.width && resolutions_[i].height == settings_.height &&
                (!fullscreen_ || resolutions_[i].native)) {
                resolutionIndex_ = static_cast<int>(i); matched = true; break;
            }
        if (!matched && fullscreen_) {
            const auto* desktop = glfwGetVideoMode(monitors_[static_cast<size_t>(selectedMonitor_)].handle);
            for (size_t i = 0; desktop && i < resolutions_.size(); ++i)
                if (resolutions_[i].width == desktop->width && resolutions_[i].height == desktop->height)
                    resolutionIndex_ = static_cast<int>(i);
        }
    }

    void applyDisplay() {
        if (fullscreen_ && !resolutions_[static_cast<size_t>(resolutionIndex_)].native) {
            const auto* desktop = glfwGetVideoMode(monitors_[static_cast<size_t>(selectedMonitor_)].handle);
            for (size_t i = 0; desktop && i < resolutions_.size(); ++i)
                if (resolutions_[i].width == desktop->width && resolutions_[i].height == desktop->height)
                    resolutionIndex_ = static_cast<int>(i);
        }
        const auto& resolution = resolutions_[static_cast<size_t>(resolutionIndex_)];
        if (fullscreen_) {
            if (!glfwGetWindowMonitor(window_)) glfwGetWindowPos(window_, &windowX_, &windowY_);
            glfwSetWindowMonitor(window_, monitors_[static_cast<size_t>(selectedMonitor_)].handle, 0, 0,
                resolution.width, resolution.height, resolution.refresh);
        } else {
            glfwSetWindowMonitor(window_, nullptr, windowX_, windowY_,
                resolution.width, resolution.height, GLFW_DONT_CARE);
        }
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        width_ = width; height_ = height;
        resetBuffers(); frame_ = 0;
        settings_.fullscreen = fullscreen_;
        settings_.width = resolution.width;
        settings_.height = resolution.height;
        settings_.monitor = monitors_[static_cast<size_t>(selectedMonitor_)].name;
        glfwSwapInterval(vsync_ ? 1 : 0);
        if (persistSettings_) saveSettings(settings_);
    }

    void drawUi() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const ImGuiIO& io = ImGui::GetIO();
        if (menuVisible_) {
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(5, 8, 16, 220));
            const float width = std::min(640.0f, io.DisplaySize.x - 32.0f);
            const float height = std::min(570.0f, io.DisplaySize.y - 32.0f);
            ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - width) * 0.5f, (io.DisplaySize.y - height) * 0.5f));
            ImGui::SetNextWindowSize(ImVec2(width, height));
            ImGui::Begin(kAppName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
            ImGui::TextUnformatted("Choose a shader");
            ImGui::InputTextWithHint("##shader_search", "Search names or IDs...", search_, sizeof(search_));
            ImGui::Separator();
            const float listHeight = std::max(120.0f, height - 320.0f);
            if (ImGui::BeginChild("Shader list", ImVec2(0, listHeight), true)) {
                std::string query = search_;
                std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                for (size_t i = 0; i < shaders_.size(); ++i) {
                    const auto& shader = shaders_[i];
                    std::string searchable = shader.name + " " + shader.id;
                    std::transform(searchable.begin(), searchable.end(), searchable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!query.empty() && searchable.find(query) == std::string::npos) continue;
                    ImGui::PushID(static_cast<int>(i));
                    if (shader.preview) ImGui::Image(static_cast<ImTextureID>(shader.preview), ImVec2(128, 72));
                    else {
                        const ImVec2 start = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(128, 72));
                        ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + 128, start.y + 72), IM_COL32(32, 36, 50, 255));
                        ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + 24, start.y + 28), IM_COL32(180, 190, 210, 255), "No preview");
                    }
                    ImGui::SameLine();
                    const std::string label = shader.name + "  [" + shader.id + "]##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selected_ == i, 0, ImVec2(0, 72))) {
                        if (select(i)) menuVisible_ = false;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::Spacing();
            ImGui::TextUnformatted("Display mode");
            if (ImGui::BeginCombo("Monitor", monitors_[static_cast<size_t>(selectedMonitor_)].name.c_str())) {
                for (size_t i = 0; i < monitors_.size(); ++i) {
                    const auto label = monitors_[i].name + "##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selectedMonitor_ == static_cast<int>(i))) {
                        selectedMonitor_ = static_cast<int>(i);
                        buildResolutions();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox("Full screen", &fullscreen_) && fullscreen_ &&
                !resolutions_[static_cast<size_t>(resolutionIndex_)].native) {
                const auto* desktop = glfwGetVideoMode(monitors_[static_cast<size_t>(selectedMonitor_)].handle);
                for (size_t i = 0; desktop && i < resolutions_.size(); ++i)
                    if (resolutions_[i].width == desktop->width && resolutions_[i].height == desktop->height)
                        resolutionIndex_ = static_cast<int>(i);
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("Resolution", (std::to_string(resolutions_[static_cast<size_t>(resolutionIndex_)].width) + " x " +
                std::to_string(resolutions_[static_cast<size_t>(resolutionIndex_)].height)).c_str())) {
                for (size_t i = 0; i < resolutions_.size(); ++i) {
                    if (fullscreen_ && !resolutions_[i].native) continue;
                    const auto label = std::to_string(resolutions_[i].width) + " x " + std::to_string(resolutions_[i].height);
                    if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == resolutionIndex_)) resolutionIndex_ = static_cast<int>(i);
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Apply display settings")) applyDisplay();
            ImGui::SameLine();
            if (ImGui::Button("Run selected shader")) menuVisible_ = false;
            ImGui::SameLine();
            if (ImGui::Button("Quit")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            if (ImGui::Checkbox("VSync (match display refresh)", &vsync_)) {
                glfwSwapInterval(vsync_ ? 1 : 0);
                settings_.vsync = vsync_;
                if (persistSettings_) saveSettings(settings_);
            }
            const auto* activeMode = glfwGetVideoMode(monitors_[static_cast<size_t>(selectedMonitor_)].handle);
            if (activeMode) {
                ImGui::SameLine();
                ImGui::TextDisabled("%d Hz", activeMode->refreshRate);
            }
            ImGui::Separator();
            if (ImGui::Button(paused_ ? "Resume [P]" : "Pause [P]")) paused_ = !paused_;
            ImGui::SameLine();
            if (ImGui::Button("Screenshot [F12]")) captureRequested_ = true;
            ImGui::SliderFloat("Time speed", &timeSpeed_, 0.1f, 3.0f, "%.1fx");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                settings_.timeSpeed = timeSpeed_;
                if (persistSettings_) saveSettings(settings_);
            }
            if (ImGui::Checkbox("Play audio", &musicOutput_)) {
                if (musicOutput_) audio_.seek(shaderTime_);
                if (!audio_.setOutput(musicOutput_)) musicOutput_ = false;
            }
            bool embeddedAudio = false;
            for (const auto& pass : shaders_[selected_].passes)
                for (const auto& input : pass.inputs)
                    embeddedAudio |= input.type == "music" && input.filepath == "embedded:logo-spin-audio";
            ImGui::TextDisabled("Audio source: %s", audio_.soundMode() ? "shader Sound pass" :
                audio_.hasMusicFile() ? (embeddedAudio ? "embedded clip" : "local file") : "generated music");
            ImGui::TextDisabled("Esc or M: menu   Left/Right: shaders   R: recompile");
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1, 0));
            ImGui::SetNextWindowBgAlpha(0.45f);
            ImGui::Begin("Menu button", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
            if (ImGui::Button("Menu  [M]")) menuVisible_ = true;
            ImGui::End();
        }
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, io.DisplaySize.y - 12.0f), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        ImGui::Text("%.0f FPS", fps_);
        ImGui::End();
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void resetBuffers() {
        if (width_ <= 0 || height_ <= 0) glfwGetFramebufferSize(window_, &width_, &height_);
        if (width_ <= 0 || height_ <= 0) return;
        for (auto& pass : shaders_[selected_].passes) if (pass.type == "buffer") {
            makeTarget(pass.targets[0], width_, height_);
            makeTarget(pass.targets[1], width_, height_);
            pass.front = 0;
        }
        makeTarget(imageTarget_, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void loadImage(Input& input) {
        if (input.type == "video") {
            if (!video_.count) throw std::runtime_error("Embedded logo frames are unavailable");
            input.width = static_cast<int>(video_.width);
            input.height = static_cast<int>(video_.height);
            if (!input.texture) {
                const auto first = video_.frame(0);
                int channels = 0;
                stbi_set_flip_vertically_on_load(1);
                unsigned char* pixels = stbi_load_from_memory(first.data, static_cast<int>(first.size),
                    &input.width, &input.height, &channels, 4);
                if (!pixels) throw std::runtime_error("Could not decode first logo frame");
                input.texture = makeTexture(input.width, input.height, pixels);
                stbi_image_free(pixels);
                input.videoFrame = 0;
            }
            return;
        }
        if (input.type != "texture" || input.texture) return;
        fs::path path = assets_ / fs::path(input.filepath).relative_path();
        if (!fs::exists(path)) path = assets_ / fs::path(input.filepath).filename();
        if (!fs::exists(path)) {
            input.texture = makeSubstituteTexture(input.filepath, input.width, input.height);
            std::cout << "Using procedural texture for " << input.filepath << '\n';
            return;
        }
        int channels = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* pixels = stbi_load(path.string().c_str(), &input.width, &input.height, &channels, 4);
        if (!pixels) { std::cerr << "Cannot read texture " << path.string() << '\n'; return; }
        input.texture = makeTexture(input.width, input.height, pixels);
        stbi_image_free(pixels);
    }

    void updateVideo(Input& input, float time) {
        const double frames = std::max(0.0, double(time)) * video_.fpsNumerator / video_.fpsDenominator;
        const int index = static_cast<int>(std::fmod(frames, double(video_.count)));
        if (index == input.videoFrame) return;
        const auto frame = video_.frame(static_cast<unsigned>(index));
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* pixels = stbi_load_from_memory(frame.data, static_cast<int>(frame.size),
            &width, &height, &channels, 4);
        if (!pixels || width != input.width || height != input.height) {
            stbi_image_free(pixels);
            throw std::runtime_error("Could not decode logo frame");
        }
        glBindTexture(GL_TEXTURE_2D, input.texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);
        input.videoFrame = index;
    }

    static void uniform1i(GLuint program, const std::string& name, int value) {
        GLint location = glGetUniformLocation(program, name.c_str());
        if (location >= 0) glUniform1i(location, value);
    }
    static void uniform1f(GLuint program, const char* name, float value) {
        GLint location = glGetUniformLocation(program, name);
        if (location >= 0) glUniform1f(location, value);
    }
    void uniforms(GLuint program, float time, float delta) {
        GLint location = glGetUniformLocation(program, "iResolution");
        if (location >= 0) glUniform3f(location, static_cast<float>(width_), static_cast<float>(height_), 1);
        uniform1f(program, "iTime", time);
        uniform1f(program, "iTimeDelta", delta);
        uniform1f(program, "iFrameRate", delta > 0 ? 1.0f / delta : 0);
        uniform1f(program, "iSampleRate", 44100);
        uniform1i(program, "iFrame", frame_);
        int clientW = 1, clientH = 1;
        glfwGetWindowSize(window_, &clientW, &clientH);
        float mx = static_cast<float>(mouseX_ * width_ / std::max(clientW, 1));
        float my = static_cast<float>((clientH - mouseY_) * height_ / std::max(clientH, 1));
        float cx = static_cast<float>(clickX_ * width_ / std::max(clientW, 1));
        float cy = static_cast<float>((clientH - clickY_) * height_ / std::max(clientH, 1));
        location = glGetUniformLocation(program, "iMouse");
        if (location >= 0) glUniform4f(location, mx, my, mouseDown_ ? cx : -cx, mouseDown_ ? cy : -cy);
        const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm calendar{};
#ifdef _WIN32
        localtime_s(&calendar, &stamp);
#else
        localtime_r(&stamp, &calendar);
#endif
        location = glGetUniformLocation(program, "iDate");
        if (location >= 0) glUniform4f(location, static_cast<float>(calendar.tm_year + 1900),
            static_cast<float>(calendar.tm_mon + 1), static_cast<float>(calendar.tm_mday),
            static_cast<float>(calendar.tm_hour * 3600 + calendar.tm_min * 60 + calendar.tm_sec));
    }

    GLFWwindow* window_;
    std::vector<Shader> shaders_;
    fs::path assets_;
    Settings settings_;
    AudioEngine audio_;
    VideoFrames video_;
    size_t selected_ = 0;
    GLuint vao_ = 0, black_ = 0, keyboard_ = 0, musicTexture_ = 0;
    Target imageTarget_, soundTarget_;
    std::uint64_t nextSoundSample_ = 0;
    std::array<unsigned char, 256 * 3 * 4> keyboardPixels_{};
    int width_ = 0, height_ = 0, frame_ = 0;
    double last_ = 0, mouseX_ = 0, mouseY_ = 0, clickX_ = 0, clickY_ = 0;
    bool mouseDown_ = false;
    bool uiEnabled_ = false, persistSettings_ = false, menuVisible_ = true, fullscreen_ = false, vsync_ = true;
    bool paused_ = false, captureRequested_ = false, musicOutput_ = false;
    int resolutionIndex_ = 0, windowX_ = 100, windowY_ = 100;
    int selectedMonitor_ = 0;
    float fps_ = 0, timeSpeed_ = 1.0f, shaderTime_ = 0.0f;
    char search_[128]{};
    std::vector<Monitor> monitors_;
    std::vector<Resolution> resolutions_;
};

int main(int argc, char** argv) {
    int result = 0;
    try {
        std::optional<fs::path> source;
        fs::path assets;
        std::string choice;
        bool validate = false, validateAll = false, validateUi = false, validateControls = false,
             validateSound = false, validateAudioDevice = false;
        std::optional<fs::path> previewDirectory;
        std::optional<fs::path> uiScreenshot;
        std::optional<fs::path> benchmarkOutput;
        bool benchmarkAll = false;
        double benchmarkSeconds = 3.0, benchmarkWarmup = 1.0;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--shader" && i + 1 < argc) choice = argv[++i];
            else if (argument == "--assets" && i + 1 < argc) assets = argv[++i];
            else if (argument == "--json" && i + 1 < argc) source = fs::path(argv[++i]);
            else if (argument == "--validate") validate = true;
            else if (argument == "--validate-all") validateAll = true;
            else if (argument == "--validate-ui") validateUi = true;
            else if (argument == "--validate-controls") validateControls = true;
            else if (argument == "--validate-sound") validateSound = true;
            else if (argument == "--validate-audio-device") validateAudioDevice = true;
            else if (argument == "--generate-previews" && i + 1 < argc) previewDirectory = fs::path(argv[++i]);
            else if (argument == "--ui-screenshot" && i + 1 < argc) uiScreenshot = fs::path(argv[++i]);
            else if (argument == "--benchmark" && i + 1 < argc) benchmarkOutput = fs::path(argv[++i]);
            else if (argument == "--benchmark-all") benchmarkAll = true;
            else if (argument == "--benchmark-seconds" && i + 1 < argc) benchmarkSeconds = std::stod(argv[++i]);
            else if (argument == "--benchmark-warmup" && i + 1 < argc) benchmarkWarmup = std::stod(argv[++i]);
            else if (!argument.empty() && argument[0] != '-' && !source) source = fs::path(argument);
            else throw std::runtime_error("Unknown or incomplete option: " + argument);
        }
        if (benchmarkAll && !benchmarkOutput) throw std::runtime_error("--benchmark-all requires --benchmark");
        if (!std::isfinite(benchmarkSeconds) || benchmarkSeconds <= 0 || benchmarkSeconds > 300 ||
            !std::isfinite(benchmarkWarmup) || benchmarkWarmup < 0 || benchmarkWarmup > 60)
            throw std::runtime_error("Benchmark duration must be 0-300 seconds and warmup 0-60 seconds");
        if (assets.empty()) assets = source ? source->parent_path() : fs::absolute(argv[0]).parent_path();
        json root;
        if (source) {
            std::ifstream file(*source);
            if (!file) throw std::runtime_error("Cannot open " + source->string());
            root = json::parse(file);
        } else {
            root = json::parse(embeddedShadersJson());
        }
        auto shaders = loadJson(root);
        if (shaders.empty()) throw std::runtime_error("No image or buffer passes found");
        Settings settings = loadSettings();
        const size_t shaderCount = shaders.size();
        size_t selected = 0;
        const bool explicitChoice = !choice.empty();
        if (choice.empty()) choice = settings.shaderId;
        if (!choice.empty()) {
            auto found = std::find_if(shaders.begin(), shaders.end(), [&](const Shader& shader) {
                return shader.id == choice || shader.name.find(choice) != std::string::npos;
            });
            if (found == shaders.end()) {
                if (explicitChoice) throw std::runtime_error("Shader not found: " + choice);
            } else selected = static_cast<size_t>(found - shaders.begin());
        }
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        if (validate || validateAll || validateUi || validateControls || validateSound || validateAudioDevice || previewDirectory || uiScreenshot)
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        GLFWwindow* window = glfwCreateWindow(previewDirectory || validateControls || validateSound ? 256 :
            benchmarkOutput ? 1280 : settings.width,
            previewDirectory || validateControls || validateSound ? 144 : benchmarkOutput ? 720 : settings.height,
            kAppName, nullptr, nullptr);
        if (!window) { glfwTerminate(); throw std::runtime_error("Could not create OpenGL 3.3 window"); }
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            glfwDestroyWindow(window); glfwTerminate(); throw std::runtime_error("Could not load OpenGL");
        }
        glfwSwapInterval(benchmarkOutput ? 0 : settings.vsync ? 1 : 0);
        {
            App app(window, std::move(shaders), assets,
                !previewDirectory && !validateControls && !validateSound && !validateAudioDevice &&
                    (validateUi || uiScreenshot || (!validate && !validateAll && !benchmarkOutput)),
                !previewDirectory && !validateControls && !validateSound && !validateAudioDevice &&
                    !validate && !validateAll && !validateUi && !uiScreenshot && !benchmarkOutput, settings);
            if (benchmarkOutput) {
                int width = 0, height = 0;
                glfwGetFramebufferSize(window, &width, &height);
                const auto glText = [](GLenum name) {
                    const auto* value = glGetString(name);
                    return value ? reinterpret_cast<const char*>(value) : "unknown";
                };
                json report = {{"schema_version", 1}, {"timestamp_utc_unix", std::time(nullptr)},
                    {"app", kAppName}, {"gpu_vendor", glText(GL_VENDOR)},
                    {"gpu_renderer", glText(GL_RENDERER)}, {"opengl_version", glText(GL_VERSION)},
                    {"resolution", {{"width", width}, {"height", height}}},
                    {"vsync", false}, {"warmup_seconds", benchmarkWarmup},
                    {"target_seconds", benchmarkSeconds}, {"results", json::array()}};
                for (size_t i = 0; i < shaderCount; ++i) {
                    if (benchmarkAll ? (explicitChoice && i != selected) : i != selected) continue;
                    const auto& info = root.at("shaders").at(i).at("info");
                    json entry = {{"shader_id", info.at("id")}, {"shader_name", info.at("name")}};
                    if (!app.select(i)) {
                        entry["error"] = "Shader compilation failed";
                        result = 1;
                    } else {
                        const auto warmupStart = std::chrono::steady_clock::now();
                        do {
                            glfwPollEvents(); app.render(); glFinish();
                        } while (!glfwWindowShouldClose(window) &&
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - warmupStart).count() < benchmarkWarmup);
                        std::vector<double> frameMs;
                        const auto measuredStart = std::chrono::steady_clock::now();
                        while (!glfwWindowShouldClose(window) &&
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - measuredStart).count() < benchmarkSeconds) {
                            glfwPollEvents();
                            const auto frameStart = std::chrono::steady_clock::now();
                            app.render(); glFinish();
                            frameMs.push_back(std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - frameStart).count());
                        }
                        if (frameMs.empty()) { entry["error"] = "Benchmark stopped before measurement"; result = 1; }
                        else entry["metrics"] = frameStatistics(std::move(frameMs));
                    }
                    report["results"].push_back(entry);
                    std::cout << entry.dump() << '\n';
                    if (glfwWindowShouldClose(window)) break;
                }
                if (benchmarkOutput->has_parent_path()) fs::create_directories(benchmarkOutput->parent_path());
                std::ofstream file(*benchmarkOutput);
                if (!file) throw std::runtime_error("Cannot write benchmark report: " + benchmarkOutput->string());
                file << report.dump(2) << '\n';
                if (!file) throw std::runtime_error("Failed to write benchmark report: " + benchmarkOutput->string());
                std::cout << "Saved benchmark: " << benchmarkOutput->string() << '\n';
            } else if (previewDirectory) {
                for (size_t i = 0; i < shaderCount; ++i) {
                    if (!app.select(i)) { result = 1; continue; }
                    app.setPreviewTime(root.at("shaders").at(i).at("info").at("id") == "N11B01" ? 0.1f : 2.5f);
                    for (int frame = 0; frame < 4; ++frame) app.render();
                    const auto path = *previewDirectory / (root.at("shaders").at(i).at("info").at("id").get<std::string>() + ".png");
                    if (!app.savePreview(path)) result = 1;
                    else std::cout << "Saved preview: " << path.string() << '\n';
                }
            } else if (validateSound) {
                for (size_t i = 0; i < shaderCount; ++i) {
                    if (explicitChoice && i != selected) continue;
                    if (!app.select(i)) { result = 1; continue; }
                    if (!app.hasSoundPass()) continue;
                    const auto started = std::chrono::steady_clock::now();
                    const float energy = app.validateSound();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    std::cout << "Sound energy " << root.at("shaders").at(i).at("info").at("id")
                              << ": " << energy << " (" << elapsed << " ms for 8192 samples)\n";
                    if (!std::isfinite(energy) || energy < 0.0001f) result = 1;
                }
            } else if (validateAll) {
                for (size_t i = 0; i < shaderCount; ++i) {
                    if (!app.select(i)) { result = 1; continue; }
                    app.render();
                }
            } else if (!app.select(selected)) result = 1;
            else if (validateAudioDevice) {
                if (!app.validateAudioDevice()) result = 1;
                std::cout << (result == 0 ? "Audio output device validated\n" : "Audio output device unavailable\n");
            }
            else if (validateControls) {
                app.render();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                app.render();
                const float before = app.shaderTime();
                const int frame = app.frameCount();
                app.setPausedForValidation(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                app.render();
                if (app.shaderTime() != before || app.frameCount() != frame) result = 1;
                app.setTimeSpeedForValidation(2.0f);
                app.setPausedForValidation(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                app.render();
                if (app.shaderTime() <= before + 0.05f || app.frameCount() != frame + 1) result = 1;
                if (!app.savePreview("build/controls-qa.png")) result = 1;
                std::cout << (result == 0 ? "Playback controls validated\n" : "Playback controls failed\n");
            }
            else if (uiScreenshot) { app.render(); if (!app.saveUiFrame(*uiScreenshot)) result = 1; }
            else if (validate || validateUi) app.render();
            else {
                app.restoreDisplay();
                while (!glfwWindowShouldClose(window)) { glfwPollEvents(); app.render(); }
            }
        }
        glfwDestroyWindow(window);
        glfwTerminate();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return result;
}
