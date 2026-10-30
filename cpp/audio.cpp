#define MINIAUDIO_IMPLEMENTATION
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>

namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int sampleRate = 44100;
}

AudioEngine::~AudioEngine() {
    if (playbackReady_) ma_device_uninit(&playback_);
}

bool AudioEngine::loadMusic(const std::filesystem::path& file) {
    if (!musicSamples_.empty()) return true;
    if (!std::filesystem::exists(file)) return false;
    ma_decoder decoder{};
    const auto config = ma_decoder_config_init(ma_format_f32, 1, sampleRate);
    if (ma_decoder_init_file(file.string().c_str(), &config, &decoder) != MA_SUCCESS) {
        std::cerr << "Could not decode audio: " << file.string() << '\n';
        return false;
    }
    std::array<float, 4096> block{};
    ma_uint64 count = 0;
    while (musicSamples_.size() < static_cast<size_t>(sampleRate * 600)) {
        count = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, block.data(), block.size(), &count);
        musicSamples_.insert(musicSamples_.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
        if (result != MA_SUCCESS || count == 0) break;
    }
    ma_decoder_uninit(&decoder);
    std::cout << "Loaded music: " << file.string() << '\n';
    return !musicSamples_.empty();
}

void AudioEngine::clearMusic() { musicSamples_.clear(); }

bool AudioEngine::loadMusicMemory(const void* data, std::size_t size) {
    if (!data || !size) return false;
    ma_decoder decoder{};
    const auto config = ma_decoder_config_init(ma_format_f32, 1, sampleRate);
    if (ma_decoder_init_memory(data, size, &config, &decoder) != MA_SUCCESS) return false;
    std::array<float, 4096> block{};
    ma_uint64 count = 0;
    while (musicSamples_.size() < static_cast<size_t>(sampleRate * 600)) {
        count = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, block.data(), block.size(), &count);
        musicSamples_.insert(musicSamples_.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
        if (result != MA_SUCCESS || count == 0) break;
    }
    ma_decoder_uninit(&decoder);
    return !musicSamples_.empty();
}

bool AudioEngine::setOutput(bool enabled) {
    if (enabled && !playbackReady_) {
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = sampleRate;
        config.dataCallback = playbackCallback;
        config.pUserData = this;
        if (ma_device_init(nullptr, &config, &playback_) != MA_SUCCESS) return false;
        playbackReady_ = true;
    }
    outputEnabled_ = enabled;
    if (enabled && !paused_ && !playbackStarted_) {
        playbackStarted_ = ma_device_start(&playback_) == MA_SUCCESS;
        return playbackStarted_;
    }
    if (!enabled && playbackStarted_) {
        ma_device_stop(&playback_);
        playbackStarted_ = false;
    }
    return true;
}

void AudioEngine::setSoundMode(bool enabled) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    soundQueue_.clear();
    soundPhase_ = 0.0f;
    soundMode_.store(enabled);
}

void AudioEngine::enqueueSound(const float* rgbaSamples, std::size_t frames) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    for (std::size_t i = 0; i < frames; ++i) {
        if (soundQueue_.size() >= 65536) break;
        const float left = std::isfinite(rgbaSamples[i * 4]) ? std::clamp(rgbaSamples[i * 4], -1.0f, 1.0f) : 0.0f;
        const float right = std::isfinite(rgbaSamples[i * 4 + 1]) ? std::clamp(rgbaSamples[i * 4 + 1], -1.0f, 1.0f) : 0.0f;
        soundQueue_.push_back({left, right});
    }
}

std::size_t AudioEngine::queuedSoundFrames() const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    return soundQueue_.size();
}

void AudioEngine::setPaused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    if (paused_ && playbackStarted_) {
        ma_device_stop(&playback_);
        playbackStarted_ = false;
    } else if (!paused_ && playbackReady_ && outputEnabled_ && !playbackStarted_) {
        playbackStarted_ = ma_device_start(&playback_) == MA_SUCCESS;
    }
}

void AudioEngine::seek(float time) {
    playbackCursor_.store(static_cast<std::uint64_t>(std::max(time, 0.0f) * sampleRate));
    if (soundMode_.load()) {
        std::lock_guard<std::mutex> lock(soundMutex_);
        soundQueue_.clear();
        soundPhase_ = 0.0f;
    }
}

float AudioEngine::sampleMusic(std::uint64_t frame) const {
    if (!musicSamples_.empty()) return musicSamples_[static_cast<size_t>(frame % musicSamples_.size())];
    const float t = static_cast<float>(frame) / sampleRate;
    constexpr float notes[] = {110.0f, 146.83f, 164.81f, 196.0f, 220.0f, 164.81f, 146.83f, 130.81f};
    const int note = static_cast<int>(t * 2.0f) % 8;
    const float decay = std::exp(-std::fmod(t * 2.0f, 1.0f) * 2.0f);
    const float phase = 2.0f * pi * notes[note] * t;
    return 0.22f * decay * (std::sin(phase) + 0.35f * std::sin(2.0f * phase));
}

void AudioEngine::playbackCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<AudioEngine*>(device->pUserData);
    auto* samples = static_cast<float*>(output);
    const float speed = self->playbackSpeed_.load();
    if (self->soundMode_.load()) {
        std::lock_guard<std::mutex> lock(self->soundMutex_);
        for (ma_uint32 i = 0; i < frames; ++i) {
            const auto pair = self->soundQueue_.empty() ? std::array<float, 2>{0, 0} : self->soundQueue_.front();
            samples[i * 2] = pair[0];
            samples[i * 2 + 1] = pair[1];
            self->soundPhase_ += speed;
            int advances = static_cast<int>(self->soundPhase_);
            self->soundPhase_ -= static_cast<float>(advances);
            while (advances-- > 0 && !self->soundQueue_.empty()) self->soundQueue_.pop_front();
        }
        return;
    }
    const auto first = self->playbackCursor_.fetch_add(static_cast<std::uint64_t>(frames * speed));
    for (ma_uint32 i = 0; i < frames; ++i) {
        const float value = self->sampleMusic(first + static_cast<std::uint64_t>(i * speed));
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
}

void AudioEngine::fillSpectrum(const std::array<float, 1024>& samples,
                              std::array<float, width * height>& pixels) {
    std::array<std::complex<float>, 1024> transformed{};
    for (size_t i = 0; i < transformed.size(); ++i) {
        const float window = 0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(i) / 1023.0f);
        transformed[i] = samples[i] * window;
    }
    for (size_t i = 1, j = 0; i < transformed.size(); ++i) {
        size_t bit = transformed.size() >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(transformed[i], transformed[j]);
    }
    for (size_t length = 2; length <= transformed.size(); length <<= 1) {
        const auto root = std::polar(1.0f, -2.0f * pi / static_cast<float>(length));
        for (size_t offset = 0; offset < transformed.size(); offset += length) {
            std::complex<float> factor(1.0f, 0.0f);
            for (size_t i = 0; i < length / 2; ++i) {
                const auto even = transformed[offset + i];
                const auto odd = transformed[offset + i + length / 2] * factor;
                transformed[offset + i] = even + odd;
                transformed[offset + i + length / 2] = even - odd;
                factor *= root;
            }
        }
    }
    for (int i = 0; i < width; ++i) {
        const float magnitude = std::abs(transformed[static_cast<size_t>(i)]) / 80.0f;
        pixels[static_cast<size_t>(i)] = std::clamp(magnitude, 0.0f, 1.0f);
        pixels[static_cast<size_t>(width + i)] = samples[static_cast<size_t>(i * 2)] * 0.5f + 0.5f;
    }
}

void AudioEngine::update(float time) {
    std::array<float, 1024> music{};
    const auto cursor = static_cast<std::uint64_t>(std::max(time, 0.0f) * sampleRate);
    for (size_t i = 0; i < music.size(); ++i) music[i] = sampleMusic(cursor + i);
    fillSpectrum(music, musicPixels_);
}
