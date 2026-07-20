# GMod Voice Capture

Reusable server-side Garry's Mod x86-64 module for selective Steam Voice
capture. Lua explicitly starts a session for one player; the module intercepts
only that player's packets during the session, decodes Opus, accumulates PCM16
mono audio and returns a WAV binary string to Lua.

It is designed as a generic building block for server-side Lua addons. It
performs no HTTP requests and requires no Node.js, Python, localhost service or
client-side DLL.

## Features

- Explicit start, stop and cancellation per player.
- Independent bounded sessions for multiple players.
- Steam Voice decoding to PCM16 mono at 24 kHz.
- WAV delivery as a binary Lua string.
- Maximum-duration and silence-timeout limits.
- Automatic cleanup when a player disconnects.
- Optional diagnostic WAV saving and debug logs.

## Install a release

1. Close Garry's Mod.
2. Download `gmsv_gmod_voice_capture_win64.dll` from the Releases page.
3. Copy it to `garrysmod/lua/bin/gmsv_gmod_voice_capture_win64.dll`.
4. Install `lua/autorun/server/gmod_voice_capture_loader.lua` in the server addon
   that will use the module.
5. Select the Garry's Mod `x86-64` branch.
6. Start a server with `sv_use_steam_voice 1`.

For a listen server, only the host installs the module. Dedicated servers install
the matching server binary. Joining players do not install anything. If the
consumer addon already includes the supplied Lua loader, only the DLL needs to be
installed separately.

## Lua API

The supplied server-side loader exposes `GModVoiceCapture`. Include the loader in
your addon and listen for `GModVoiceCaptureCompleted`; the returned `wavData` is a
binary Lua string, so do not treat it as UTF-8 text.

### Minimal capture

```lua
local available, reason = GModVoiceCapture.IsAvailable()
if not available then
    print("Voice capture unavailable: " .. tostring(reason))
    return
end

hook.Add("GModVoiceCaptureCompleted", "MyAddon.HandleVoice", function(ply, wavData, metadata)
    print(string.format(
        "Captured %.2f seconds from %s (%d Hz, %d packets, %d bytes)",
        metadata.duration,
        ply:Nick(),
        metadata.sampleRate,
        metadata.packets,
        #wavData
    ))

    -- Optional diagnostic save. file.Write paths are relative to garrysmod/data/.
    file.CreateDir("my_addon_voice")
    file.Write("my_addon_voice/latest.wav", wavData)
end)

local function beginVoiceCapture(ply)
    local ok, err = GModVoiceCapture.StartCapture(ply)
    if not ok then
        print("Could not start capture: " .. tostring(err))
    end
    return ok
end

-- Call this when your push-to-talk interaction ends. The completed hook runs
-- asynchronously after the native queue is polled by the loader.
local function endVoiceCapture(ply)
    local stopped, stopError = GModVoiceCapture.StopCapture(ply)
    if not stopped then
        print("Could not stop capture: " .. tostring(stopError))
    end
    return stopped
end

-- Call beginVoiceCapture(ply) and endVoiceCapture(ply) from your own validated,
-- server-side interaction or net-message handlers.
```

Each player has an independent bounded session. Completed audio is PCM16 mono at
24 kHz in a WAV container. Sessions have configurable maximum duration and
silence timeout, are cancelled on disconnect and never retain voice outside an
explicit capture window.

The metadata table currently contains:

```text
userID, steamID64, duration, sampleRate, channels, bitsPerSample,
packets, bytes, decodeErrors, reason, truncated, wavData
```

### Cancel without delivering audio

Use cancellation when an interaction becomes invalid, for example when the
target NPC is removed or the player walks out of range:

```lua
if IsValid(ply) then
    GModVoiceCapture.CancelCapture(ply)
end
```

Cancellation discards that session and does not run
`GModVoiceCaptureCompleted` for it.

### Multiple players

Sessions are stored independently by player UserID. Starting captures for two
players does not mix their decoded audio:

```lua
for _, ply in ipairs(player.GetHumans()) do
    local ok, err = GModVoiceCapture.StartCapture(ply)
    if not ok then
        print("Capture failed for " .. ply:Nick() .. ": " .. tostring(err))
    end
end

-- Stop each session independently later.
for _, ply in ipairs(player.GetHumans()) do
    GModVoiceCapture.StopCapture(ply)
end
```

Only start sessions in response to an explicit player interaction. The module
receives server voice packets, but retains decoded audio only for players whose
capture session is active.

### Configuration

The loader keeps the native limits synchronized with these server ConVars:

```text
gmod_voice_capture_max_duration 10
gmod_voice_capture_silence_timeout 0.7
```

`gmod_voice_capture_max_duration` is clamped to 1–30 seconds. A silence timeout
of `0` disables automatic completion. These limits apply globally; consumer
addons should additionally enforce their own permissions, distance checks and
cooldowns before calling `StartCapture`.

## Diagnostic test

1. Start an x86-64 server with `sv_use_steam_voice 1`.
2. Run `gmod_voice_capture_test_start` from the listen-host console.
3. Hold normal voice chat and speak for several seconds.
4. Run `gmod_voice_capture_test_stop` if silence timeout has not stopped it.
5. Listen to the WAV under `garrysmod/data/gmod_voice_capture_test/`.

Useful commands and ConVars:

```text
gmod_voice_capture_test_players
gmod_voice_capture_test_start_userid <userid>
gmod_voice_capture_test_stop_userid <userid>
gmod_voice_capture_debug 0/1
gmod_voice_capture_save_test_wav 0/1
gmod_voice_capture_max_duration 10
gmod_voice_capture_silence_timeout 0.7
```

## Windows x64 build

Run from PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build_windows_x64.ps1
```

The script pins `garrysmod_common`, Premake 5.0.0-beta8 and official Opus 1.6.1,
builds Opus statically, generates a Visual Studio 2022 project and produces:

```text
bin/windows-x64/gmsv_gmod_voice_capture_win64.dll
```

Windows x64 is currently supported. Linux x64 support is not yet available.

## License and provenance

The module is LGPL-2.1-only because its Steam Voice framing and Opus sequence
handling are adapted from
[Meachamp/gm_8bit](https://github.com/Meachamp/gm_8bit). Opus is BSD licensed;
`garrysmod_common` is BSD-3-Clause and contains Source SDK licensed headers. See
`THIRD_PARTY_NOTICES.md` and the SPDX headers in source files.

This implementation removes gm_8bit's relay/effects/recompression behavior and
adds bounded, selective per-player capture sessions, explicit cleanup, silence
preservation and WAV delivery to Lua.
