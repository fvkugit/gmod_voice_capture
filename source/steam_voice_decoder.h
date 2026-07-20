// SPDX-License-Identifier: LGPL-2.1-only
// Steam Voice framing and sequence handling are adapted from gm_8bit:
// https://github.com/Meachamp/gm_8bit (Copyright its contributors).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <opus.h>

namespace gmod_voice_capture {

constexpr int kSampleRate = 24000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;

class SteamVoiceDecoder {
public:
    SteamVoiceDecoder() {
        int error = OPUS_OK;
        decoder_ = opus_decoder_create(kSampleRate, kChannels, &error);
        if (error != OPUS_OK) decoder_ = nullptr;
    }

    ~SteamVoiceDecoder() {
        if (decoder_) opus_decoder_destroy(decoder_);
    }

    SteamVoiceDecoder(const SteamVoiceDecoder&) = delete;
    SteamVoiceDecoder& operator=(const SteamVoiceDecoder&) = delete;

    bool IsValid() const { return decoder_ != nullptr; }

    void Reset() {
        if (decoder_) opus_decoder_ctl(decoder_, OPUS_RESET_STATE);
        expectedSequence_ = 0;
    }

    bool DecodePacket(const char* packet, std::size_t packetLength,
        std::vector<std::int16_t>& output, std::size_t maxSamples,
        std::uint32_t& streamSampleRate) {
        constexpr std::size_t kSteamIdBytes = sizeof(std::uint64_t);
        constexpr std::size_t kCrcBytes = sizeof(std::uint32_t);
        if (!decoder_ || !packet || packetLength < kSteamIdBytes + kCrcBytes) return false;

        const auto* cursor = reinterpret_cast<const std::uint8_t*>(packet) + kSteamIdBytes;
        const auto* end = reinterpret_cast<const std::uint8_t*>(packet) + packetLength - kCrcBytes;

        while (cursor < end) {
            const std::uint8_t opcode = *cursor++;
            switch (opcode) {
            case 0: { // OP_SILENCE
                std::uint16_t silenceSamples = 0;
                if (!Read(cursor, end, silenceSamples)) return false;
                if (!AppendSilence(output, silenceSamples, maxSamples)) return false;
                break;
            }
            case 11: { // OP_SAMPLERATE
                std::uint16_t sampleRate = 0;
                if (!Read(cursor, end, sampleRate)) return false;
                streamSampleRate = sampleRate;
                if (sampleRate != kSampleRate) return false;
                break;
            }
            case 6: { // OP_CODEC_OPUSPLC
                std::uint16_t framedLength = 0;
                if (!Read(cursor, end, framedLength)) return false;
                if (static_cast<std::size_t>(end - cursor) < framedLength) return false;
                if (!DecodeFrames(cursor, framedLength, output, maxSamples)) return false;
                cursor += framedLength;
                break;
            }
            default:
                return false;
            }
        }
        return cursor == end;
    }

private:
    template <typename T>
    static bool Read(const std::uint8_t*& cursor, const std::uint8_t* end, T& value) {
        if (static_cast<std::size_t>(end - cursor) < sizeof(T)) return false;
        std::memcpy(&value, cursor, sizeof(T));
        cursor += sizeof(T);
        return true;
    }

    static bool AppendSilence(std::vector<std::int16_t>& output,
        std::size_t count, std::size_t maxSamples) {
        if (count > maxSamples - std::min(output.size(), maxSamples)) return false;
        output.insert(output.end(), count, 0);
        return true;
    }

    bool DecodeFrames(const std::uint8_t* compressed, std::size_t compressedLength,
        std::vector<std::int16_t>& output, std::size_t maxSamples) {
        const auto* cursor = compressed;
        const auto* end = compressed + compressedLength;
        std::int16_t frame[2880] = {};

        while (cursor < end) {
            std::uint16_t frameLength = 0;
            if (!Read(cursor, end, frameLength)) return false;
            if (frameLength == 0xFFFF) {
                Reset();
                continue;
            }

            std::uint16_t sequence = 0;
            if (!Read(cursor, end, sequence)) return false;
            if (frameLength == 0 || static_cast<std::size_t>(end - cursor) < frameLength) return false;

            if (sequence < expectedSequence_) {
                Reset();
            } else if (sequence > expectedSequence_) {
                const std::uint16_t lostFrames = std::min<std::uint16_t>(sequence - expectedSequence_, 10);
                for (std::uint16_t i = 0; i < lostFrames; ++i) {
                    const int samples = opus_decode(decoder_, nullptr, 0, frame,
                        static_cast<int>(sizeof(frame) / sizeof(frame[0])), 0);
                    if (samples < 0 || !Append(output, frame, samples, maxSamples)) return false;
                }
            }

            const int samples = opus_decode(decoder_, cursor, frameLength, frame,
                static_cast<int>(sizeof(frame) / sizeof(frame[0])), 0);
            if (samples < 0 || !Append(output, frame, samples, maxSamples)) return false;

            expectedSequence_ = static_cast<std::uint16_t>(sequence + 1);
            cursor += frameLength;
        }
        return cursor == end;
    }

    static bool Append(std::vector<std::int16_t>& output, const std::int16_t* samples,
        std::size_t count, std::size_t maxSamples) {
        if (count > maxSamples - std::min(output.size(), maxSamples)) return false;
        output.insert(output.end(), samples, samples + count);
        return true;
    }

    OpusDecoder* decoder_ = nullptr;
    std::uint16_t expectedSequence_ = 0;
};

} // namespace gmod_voice_capture
