local TAG = "[GMod Voice Capture] "

local debugConVar = CreateConVar("gmod_voice_capture_debug", "0", FCVAR_ARCHIVE,
    "Enable diagnostic GMod Voice Capture logging")
local saveConVar = CreateConVar("gmod_voice_capture_save_test_wav", "1", FCVAR_ARCHIVE,
    "Save diagnostic WAV files under data/gmod_voice_capture_test")
local maxDurationConVar = CreateConVar("gmod_voice_capture_max_duration", "10", FCVAR_ARCHIVE,
    "Maximum capture duration in seconds", 1, 30)
local silenceConVar = CreateConVar("gmod_voice_capture_silence_timeout", "0.7", FCVAR_ARCHIVE,
    "Automatically finish after this many seconds without a packet; 0 disables it", 0, 5)

local function log(message)
    if debugConVar:GetBool() then MsgN(TAG .. message) end
end

local loaded, loadError = pcall(require, "gmod_voice_capture")
local native = loaded and gmod_voice_capture_native or nil

GModVoiceCapture = GModVoiceCapture or {}
local API = GModVoiceCapture
API.Native = native
API.LoadError = loaded and nil or tostring(loadError)

function API.IsAvailable()
    if not native or not native.IsAvailable then
        return false, API.LoadError or "native module was not loaded"
    end
    return native.IsAvailable()
end

local available, availabilityReason = API.IsAvailable()
if not available then
    MsgN(TAG .. "Native capture unavailable: " .. tostring(availabilityReason))
    MsgN(TAG .. "Install gmsv_gmod_voice_capture_win64.dll in garrysmod/lua/bin.")
    return
end

native.SetMaxDuration(maxDurationConVar:GetFloat())
native.SetSilenceTimeout(silenceConVar:GetFloat())
log("Module " .. tostring(native.GetVersion()) .. " is available")

cvars.AddChangeCallback("gmod_voice_capture_max_duration", function(_, _, value)
    native.SetMaxDuration(tonumber(value) or 10)
end, "GModVoiceCapture.NativeMaxDuration")

cvars.AddChangeCallback("gmod_voice_capture_silence_timeout", function(_, _, value)
    native.SetSilenceTimeout(tonumber(value) or 0.7)
end, "GModVoiceCapture.NativeSilenceTimeout")

local function validPlayer(ply)
    return IsValid(ply) and ply:IsPlayer() and not ply:IsBot()
end

function API.StartCapture(ply)
    if not validPlayer(ply) then return false, "invalid player" end
    local steamVoice = GetConVar("sv_use_steam_voice")
    if not steamVoice or not steamVoice:GetBool() then
        return false, "sv_use_steam_voice must be 1"
    end
    local ok, err = native.StartCapture(ply:UserID(), ply:SteamID64())
    if ok then
        log("Capture started for " .. ply:Nick() .. " [UserID " .. ply:UserID() .. "]")
    end
    return ok, err
end

function API.StopCapture(ply)
    if not validPlayer(ply) then return false, "invalid player" end
    return native.StopCapture(ply:UserID())
end

function API.CancelCapture(ply)
    if not validPlayer(ply) then return false end
    return native.CancelCapture(ply:UserID())
end

local function resolvePlayer(userID, steamID64)
    for _, ply in ipairs(player.GetHumans()) do
        if ply:UserID() == userID and ply:SteamID64() == steamID64 then return ply end
    end
end

local function safeFileIdentity(value)
    return tostring(value or "unknown"):gsub("[^%w_-]", "_")
end

local function deliverCapture(capture)
    local ply = resolvePlayer(capture.userID, capture.steamID64)
    if not IsValid(ply) then
        log("Discarded completed capture because its player is no longer valid")
        return
    end

    log(string.format(
        "Capture finished for %s: %.2f seconds, %d packets, %d bytes, %d decode errors (%s)",
        ply:Nick(), capture.duration, capture.packets, capture.bytes,
        capture.decodeErrors, capture.reason
    ))

    if saveConVar:GetBool() then
        file.CreateDir("gmod_voice_capture_test")
        local filename = string.format("gmod_voice_capture_test/%s_userid%d_%d.wav",
            safeFileIdentity(capture.steamID64), capture.userID, os.time())
        file.Write(filename, capture.wavData)
        log("WAV delivered to Lua and saved as data/" .. filename)
    end

    hook.Run("GModVoiceCaptureCompleted", ply, capture.wavData, capture)
end

hook.Add("Think", "GModVoiceCapture.PollNativeCapture", function()
    native.Tick()
    for _ = 1, 8 do
        local capture = native.PopCapture()
        if not capture then break end
        deliverCapture(capture)
    end
end)

hook.Add("PlayerDisconnected", "GModVoiceCapture.CancelDisconnectedCapture", function(ply)
    native.CancelCapture(ply:UserID())
end)

local function canUseDiagnostics(ply)
    if not IsValid(ply) then return true end
    return game.SinglePlayer() or ply:IsListenServerHost() or ply:IsSuperAdmin()
end

local function findHumanByUserID(userID)
    userID = tonumber(userID)
    if not userID then return nil end
    for _, target in ipairs(player.GetHumans()) do
        if target:UserID() == userID then return target end
    end
end

concommand.Add("gmod_voice_capture_test_start", function(ply)
    if not canUseDiagnostics(ply) then return end
    if not validPlayer(ply) then
        MsgN(TAG .. "Run this command from the listen-server player's console.")
        return
    end
    local ok, err = API.StartCapture(ply)
    if not ok then MsgN(TAG .. "Could not start capture: " .. tostring(err)) end
end)

concommand.Add("gmod_voice_capture_test_stop", function(ply)
    if not canUseDiagnostics(ply) then return end
    if not validPlayer(ply) then
        MsgN(TAG .. "Run this command from the listen-server player's console.")
        return
    end
    local ok, err = API.StopCapture(ply)
    if not ok then MsgN(TAG .. "Could not stop capture: " .. tostring(err)) end
end)

concommand.Add("gmod_voice_capture_test_players", function(ply)
    if not canUseDiagnostics(ply) then return end
    MsgN(TAG .. "Connected human players:")
    for _, target in ipairs(player.GetHumans()) do
        MsgN(string.format("  UserID %d: %s [%s]", target:UserID(),
            target:Nick(), target:SteamID64()))
    end
end)

concommand.Add("gmod_voice_capture_test_start_userid", function(ply, _, args)
    if not canUseDiagnostics(ply) then return end
    local target = findHumanByUserID(args[1])
    if not validPlayer(target) then
        MsgN(TAG .. "Unknown UserID. Run gmod_voice_capture_test_players first.")
        return
    end
    local ok, err = API.StartCapture(target)
    if not ok then MsgN(TAG .. "Could not start capture: " .. tostring(err)) end
end)

concommand.Add("gmod_voice_capture_test_stop_userid", function(ply, _, args)
    if not canUseDiagnostics(ply) then return end
    local target = findHumanByUserID(args[1])
    if not validPlayer(target) then
        MsgN(TAG .. "Unknown UserID. Run gmod_voice_capture_test_players first.")
        return
    end
    local ok, err = API.StopCapture(target)
    if not ok then MsgN(TAG .. "Could not stop capture: " .. tostring(err)) end
end)
