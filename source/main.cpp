// SPDX-License-Identifier: LGPL-2.1-only
#define NO_MALLOC_OVERRIDE

#include "capture_manager.h"

#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Symbol.hpp>
#include <detouring/hook.hpp>
#include <iclient.h>
#include <scanning/symbolfinder.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace LuaType = GarrysMod::Lua::Type;

namespace {

constexpr const char* kVersion = "0.2.0";

#ifdef SYSTEM_WINDOWS
const std::vector<Symbol> kBroadcastVoiceSymbols = {
#if defined ARCHITECTURE_X86_64
    Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC****\x8B\xF2\x4C\x8B\xF1"),
#endif
};
#elif defined SYSTEM_LINUX
const std::vector<Symbol> kBroadcastVoiceSymbols = {
    Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx")
};
#else
const std::vector<Symbol> kBroadcastVoiceSymbols = {};
#endif

using BroadcastVoiceData = void (*)(IClient*, int, char*, std::int64_t);

gmod_voice_capture::CaptureManager g_captureManager;
Detouring::Hook g_broadcastVoiceHook;
bool g_available = false;
std::string g_unavailableReason = "module has not initialized";

void HookBroadcastVoiceData(IClient* client, int byteCount, char* data, std::int64_t xuid) {
    if (client && data && byteCount > 0) {
        g_captureManager.OnVoicePacket(client->GetUserID(), data,
            static_cast<std::size_t>(byteCount));
    }
    g_broadcastVoiceHook.GetTrampoline<BroadcastVoiceData>()(client, byteCount, data, xuid);
}

void PushBooleanResult(GarrysMod::Lua::ILua* LUA, bool success, const std::string& error = {}) {
    LUA->PushBool(success);
    if (success) {
        LUA->PushNil();
    } else {
        LUA->PushString(error.c_str());
    }
}

void SetStringField(GarrysMod::Lua::ILua* LUA, const char* name, const std::string& value) {
    LUA->PushString(value.c_str(), static_cast<unsigned int>(value.size()));
    LUA->SetField(-2, name);
}

void SetNumberField(GarrysMod::Lua::ILua* LUA, const char* name, double value) {
    LUA->PushNumber(value);
    LUA->SetField(-2, name);
}

void SetBoolField(GarrysMod::Lua::ILua* LUA, const char* name, bool value) {
    LUA->PushBool(value);
    LUA->SetField(-2, name);
}

LUA_FUNCTION_STATIC(IsAvailable) {
    LUA->PushBool(g_available);
    LUA->PushString(g_available ? "available" : g_unavailableReason.c_str());
    return 2;
}

LUA_FUNCTION_STATIC(GetVersion) {
    LUA->PushString(kVersion);
    return 1;
}

LUA_FUNCTION_STATIC(SetMaxDuration) {
    LUA->CheckType(1, LuaType::Number);
    g_captureManager.SetMaxDuration(LUA->GetNumber(1));
    LUA->PushNumber(g_captureManager.GetMaxDuration());
    return 1;
}

LUA_FUNCTION_STATIC(SetSilenceTimeout) {
    LUA->CheckType(1, LuaType::Number);
    g_captureManager.SetSilenceTimeout(LUA->GetNumber(1));
    LUA->PushNumber(g_captureManager.GetSilenceTimeout());
    return 1;
}

LUA_FUNCTION_STATIC(StartCapture) {
    if (!g_available) {
        PushBooleanResult(LUA, false, g_unavailableReason);
        return 2;
    }
    LUA->CheckType(1, LuaType::Number);
    LUA->CheckType(2, LuaType::String);
    const int userId = static_cast<int>(LUA->GetNumber(1));
    const char* steamId64 = LUA->GetString(2);
    std::string error;
    const bool success = g_captureManager.Start(userId, steamId64 ? steamId64 : "", error);
    PushBooleanResult(LUA, success, error);
    return 2;
}

LUA_FUNCTION_STATIC(StopCapture) {
    LUA->CheckType(1, LuaType::Number);
    const int userId = static_cast<int>(LUA->GetNumber(1));
    std::string error;
    const bool success = g_captureManager.Stop(userId, "explicit_stop", error);
    PushBooleanResult(LUA, success, error);
    return 2;
}

LUA_FUNCTION_STATIC(CancelCapture) {
    LUA->CheckType(1, LuaType::Number);
    LUA->PushBool(g_captureManager.Cancel(static_cast<int>(LUA->GetNumber(1))));
    return 1;
}

LUA_FUNCTION_STATIC(Tick) {
    g_captureManager.Tick();
    return 0;
}

LUA_FUNCTION_STATIC(PopCapture) {
    gmod_voice_capture::CaptureResult result;
    if (!g_captureManager.Pop(result)) {
        LUA->PushNil();
        return 1;
    }

    LUA->CreateTable();
    SetNumberField(LUA, "userID", result.userId);
    SetStringField(LUA, "steamID64", result.steamId64);
    LUA->PushString(result.wav.data(), static_cast<unsigned int>(result.wav.size()));
    LUA->SetField(-2, "wavData");
    SetNumberField(LUA, "duration", result.duration);
    SetNumberField(LUA, "sampleRate", result.sampleRate);
    SetNumberField(LUA, "channels", gmod_voice_capture::kChannels);
    SetNumberField(LUA, "bitsPerSample", gmod_voice_capture::kBitsPerSample);
    SetNumberField(LUA, "bytes", static_cast<double>(result.wav.size()));
    SetNumberField(LUA, "packets", result.packets);
    SetNumberField(LUA, "decodeErrors", result.decodeErrors);
    SetStringField(LUA, "reason", result.reason);
    SetBoolField(LUA, "truncated", result.truncated);
    return 1;
}

void AddFunction(GarrysMod::Lua::ILua* LUA, const char* name, GarrysMod::Lua::CFunc function) {
    LUA->PushCFunction(function);
    LUA->SetField(-2, name);
}

} // namespace

GMOD_MODULE_OPEN() {
    SourceSDK::ModuleLoader engineLoader("engine");
    SymbolFinder finder;
    void* broadcastVoice = nullptr;

    for (const auto& symbol : kBroadcastVoiceSymbols) {
        broadcastVoice = finder.Resolve(engineLoader.GetModule(), symbol.name.c_str(), symbol.length);
        if (broadcastVoice) break;
    }

    if (!broadcastVoice) {
        g_available = false;
        g_unavailableReason = "SV_BroadcastVoiceData was not found in engine";
    } else {
        g_broadcastVoiceHook.Create(Detouring::Hook::Target(broadcastVoice),
            reinterpret_cast<void*>(&HookBroadcastVoiceData));
        g_broadcastVoiceHook.Enable();
        g_available = true;
        g_unavailableReason.clear();
    }

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->PushString("gmod_voice_capture_native");
    LUA->CreateTable();
    AddFunction(LUA, "IsAvailable", IsAvailable);
    AddFunction(LUA, "GetVersion", GetVersion);
    AddFunction(LUA, "SetMaxDuration", SetMaxDuration);
    AddFunction(LUA, "SetSilenceTimeout", SetSilenceTimeout);
    AddFunction(LUA, "StartCapture", StartCapture);
    AddFunction(LUA, "StopCapture", StopCapture);
    AddFunction(LUA, "CancelCapture", CancelCapture);
    AddFunction(LUA, "Tick", Tick);
    AddFunction(LUA, "PopCapture", PopCapture);
    LUA->SetTable(-3);
    LUA->Pop(1);
    return 0;
}

GMOD_MODULE_CLOSE() {
    g_available = false;
    g_captureManager.Clear();
    if (g_broadcastVoiceHook.IsValid()) {
        g_broadcastVoiceHook.Disable();
        g_broadcastVoiceHook.Destroy();
    }
    return 0;
}
