// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "steam_voice_decoder.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gmod_voice_capture {

using Clock = std::chrono::steady_clock;

struct CaptureResult {
    int userId = 0;
    std::string steamId64;
    std::string wav;
    double duration = 0.0;
    std::uint32_t packets = 0;
    std::uint32_t decodeErrors = 0;
    std::uint32_t sampleRate = kSampleRate;
    std::string reason;
    bool truncated = false;
};

struct Session {
    Session(int id, std::string steamId, double maximumDuration);

    int userId;
    std::string steamId64;
    double maxDuration;
    Clock::time_point startedAt;
    Clock::time_point lastPacketAt;
    bool receivedPacket = false;
    bool limitReached = false;
    std::uint32_t packets = 0;
    std::uint32_t decodeErrors = 0;
    std::uint32_t streamSampleRate = kSampleRate;
    SteamVoiceDecoder decoder;
    std::vector<std::int16_t> pcm;
    std::mutex mutex;
};

class CaptureManager {
public:
    bool Start(int userId, const std::string& steamId64, std::string& error);
    bool Stop(int userId, const std::string& reason, std::string& error);
    bool Cancel(int userId);
    void OnVoicePacket(int userId, const char* data, std::size_t length);
    void Tick();
    bool Pop(CaptureResult& result);
    void Clear();

    void SetMaxDuration(double seconds);
    void SetSilenceTimeout(double seconds);
    double GetMaxDuration() const;
    double GetSilenceTimeout() const;

private:
    bool Finalize(const std::shared_ptr<Session>& session, const std::string& reason,
        std::string& error);
    static std::string BuildWav(const std::vector<std::int16_t>& pcm);

    mutable std::mutex sessionsMutex_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::mutex completedMutex_;
    std::deque<CaptureResult> completed_;
    double maxDuration_ = 10.0;
    double silenceTimeout_ = 0.7;
    static constexpr std::size_t kMaxConcurrentCaptures = 8;
};

} // namespace gmod_voice_capture
