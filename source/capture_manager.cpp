// SPDX-License-Identifier: LGPL-2.1-only
#include "capture_manager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace gmod_voice_capture {

namespace {

template <typename T>
void AppendLittleEndian(std::string& output, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

double SecondsBetween(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

} // namespace

Session::Session(int id, std::string steamId, double maximumDuration)
    : userId(id), steamId64(std::move(steamId)), maxDuration(maximumDuration),
      startedAt(Clock::now()), lastPacketAt(startedAt) {
    const auto maxSamples = static_cast<std::size_t>(maxDuration * kSampleRate);
    pcm.reserve(maxSamples);
}

bool CaptureManager::Start(int userId, const std::string& steamId64, std::string& error) {
    if (userId <= 0 || steamId64.empty()) {
        error = "invalid player identity";
        return false;
    }

    std::lock_guard lock(sessionsMutex_);
    if (sessions_.find(userId) != sessions_.end()) {
        error = "capture already active";
        return false;
    }
    if (sessions_.size() >= kMaxConcurrentCaptures) {
        error = "too many concurrent captures";
        return false;
    }

    auto session = std::make_shared<Session>(userId, steamId64, maxDuration_);
    if (!session->decoder.IsValid()) {
        error = "failed to create Opus decoder";
        return false;
    }
    sessions_.emplace(userId, std::move(session));
    return true;
}

bool CaptureManager::Stop(int userId, const std::string& reason, std::string& error) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard lock(sessionsMutex_);
        const auto found = sessions_.find(userId);
        if (found == sessions_.end()) {
            error = "capture is not active";
            return false;
        }
        session = found->second;
        sessions_.erase(found);
    }
    return Finalize(session, reason, error);
}

bool CaptureManager::Cancel(int userId) {
    std::lock_guard lock(sessionsMutex_);
    return sessions_.erase(userId) > 0;
}

void CaptureManager::OnVoicePacket(int userId, const char* data, std::size_t length) {
    std::shared_ptr<Session> session;
    {
        std::lock_guard lock(sessionsMutex_);
        const auto found = sessions_.find(userId);
        if (found == sessions_.end()) return;
        session = found->second;
    }

    std::lock_guard sessionLock(session->mutex);
    if (session->limitReached) return;

    const auto maximumSamples = static_cast<std::size_t>(session->maxDuration * kSampleRate);
    session->lastPacketAt = Clock::now();
    session->receivedPacket = true;
    ++session->packets;

    if (!session->decoder.DecodePacket(data, length, session->pcm,
        maximumSamples, session->streamSampleRate)) {
        ++session->decodeErrors;
        if (session->pcm.size() >= maximumSamples) session->limitReached = true;
        return;
    }
    if (session->pcm.size() >= maximumSamples) session->limitReached = true;
}

void CaptureManager::Tick() {
    std::vector<std::pair<int, std::string>> toStop;
    const auto now = Clock::now();
    {
        std::lock_guard lock(sessionsMutex_);
        for (const auto& [userId, session] : sessions_) {
            std::lock_guard sessionLock(session->mutex);
            if (session->limitReached || SecondsBetween(session->startedAt, now) >= session->maxDuration) {
                toStop.emplace_back(userId, "max_duration");
            } else if (session->receivedPacket && silenceTimeout_ > 0.0 &&
                SecondsBetween(session->lastPacketAt, now) >= silenceTimeout_) {
                toStop.emplace_back(userId, "silence_timeout");
            }
        }
    }

    for (const auto& [userId, reason] : toStop) {
        std::string ignored;
        Stop(userId, reason, ignored);
    }
}

bool CaptureManager::Pop(CaptureResult& result) {
    std::lock_guard lock(completedMutex_);
    if (completed_.empty()) return false;
    result = std::move(completed_.front());
    completed_.pop_front();
    return true;
}

void CaptureManager::Clear() {
    {
        std::lock_guard lock(sessionsMutex_);
        sessions_.clear();
    }
    {
        std::lock_guard lock(completedMutex_);
        completed_.clear();
    }
}

void CaptureManager::SetMaxDuration(double seconds) {
    std::lock_guard lock(sessionsMutex_);
    maxDuration_ = std::clamp(seconds, 0.25, 30.0);
}

void CaptureManager::SetSilenceTimeout(double seconds) {
    std::lock_guard lock(sessionsMutex_);
    silenceTimeout_ = std::clamp(seconds, 0.0, 5.0);
}

double CaptureManager::GetMaxDuration() const {
    std::lock_guard lock(sessionsMutex_);
    return maxDuration_;
}

double CaptureManager::GetSilenceTimeout() const {
    std::lock_guard lock(sessionsMutex_);
    return silenceTimeout_;
}

bool CaptureManager::Finalize(const std::shared_ptr<Session>& session,
    const std::string& reason, std::string& error) {
    CaptureResult result;
    {
        std::lock_guard lock(session->mutex);
        if (!session->receivedPacket || session->pcm.empty()) {
            error = "capture contained no decoded audio";
            return false;
        }
        result.userId = session->userId;
        result.steamId64 = session->steamId64;
        result.wav = BuildWav(session->pcm);
        result.duration = static_cast<double>(session->pcm.size()) / kSampleRate;
        result.packets = session->packets;
        result.decodeErrors = session->decodeErrors;
        result.sampleRate = session->streamSampleRate;
        result.reason = reason;
        result.truncated = session->limitReached;
    }

    std::lock_guard completedLock(completedMutex_);
    completed_.push_back(std::move(result));
    return true;
}

std::string CaptureManager::BuildWav(const std::vector<std::int16_t>& pcm) {
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    std::string wav;
    wav.reserve(44 + dataBytes);
    wav.append("RIFF", 4);
    AppendLittleEndian<std::uint32_t>(wav, 36 + dataBytes);
    wav.append("WAVEfmt ", 8);
    AppendLittleEndian<std::uint32_t>(wav, 16);
    AppendLittleEndian<std::uint16_t>(wav, 1);
    AppendLittleEndian<std::uint16_t>(wav, kChannels);
    AppendLittleEndian<std::uint32_t>(wav, kSampleRate);
    AppendLittleEndian<std::uint32_t>(wav, kSampleRate * kChannels * sizeof(std::int16_t));
    AppendLittleEndian<std::uint16_t>(wav, kChannels * sizeof(std::int16_t));
    AppendLittleEndian<std::uint16_t>(wav, kBitsPerSample);
    wav.append("data", 4);
    AppendLittleEndian<std::uint32_t>(wav, dataBytes);
    wav.append(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return wav;
}

} // namespace gmod_voice_capture
