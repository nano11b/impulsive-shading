#pragma once

#include <miniaudio.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <vector>

class AudioEngine {
public:
    static constexpr int width = 512;
    static constexpr int height = 2;
    static constexpr int soundBlockFrames = 2048;
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool loadMusic(const std::filesystem::path& file);
    bool loadMusicMemory(const void* data, std::size_t size);
    void clearMusic();
    bool setOutput(bool enabled);
    void setSoundMode(bool enabled);
    void enqueueSound(const float* rgbaSamples, std::size_t frames);
    std::size_t queuedSoundFrames() const;
    void setPaused(bool paused);
    void setSpeed(float speed) { playbackSpeed_.store(speed); }
    void seek(float time);
    void update(float time);
    const float* musicPixels() const { return musicPixels_.data(); }
    bool hasMusicFile() const { return !musicSamples_.empty(); }
    bool soundMode() const { return soundMode_.load(); }

private:
    static void playbackCallback(ma_device* device, void* output, const void* input, ma_uint32 frames);
    float sampleMusic(std::uint64_t frame) const;
    static void fillSpectrum(const std::array<float, 1024>& samples, std::array<float, width * height>& pixels);

    std::vector<float> musicSamples_;
    ma_device playback_{};
    bool playbackReady_ = false, playbackStarted_ = false, outputEnabled_ = false;
    bool paused_ = false;
    std::atomic<std::uint64_t> playbackCursor_{0};
    std::atomic<float> playbackSpeed_{1.0f};
    std::atomic<bool> soundMode_{false};
    mutable std::mutex soundMutex_;
    std::deque<std::array<float, 2>> soundQueue_;
    float soundPhase_ = 0.0f;
    std::array<float, width * height> musicPixels_{};
};
