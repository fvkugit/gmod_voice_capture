PROJECT_GENERATOR_VERSION = 3

newoption({
    trigger = "gmcommon",
    description = "Path to garrysmod_common",
    value = "path"
})

newoption({
    trigger = "opus",
    description = "Path to the Opus source tree",
    value = "path"
})

newoption({
    trigger = "opus-build",
    description = "Path containing the built Opus library",
    value = "path"
})

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
    "Pass --gmcommon or set GARRYSMOD_COMMON")
local opus = assert(_OPTIONS.opus or os.getenv("GMOD_VOICE_CAPTURE_OPUS_ROOT"),
    "Pass --opus or set GMOD_VOICE_CAPTURE_OPUS_ROOT")
local opusBuild = assert(_OPTIONS["opus-build"] or os.getenv("GMOD_VOICE_CAPTURE_OPUS_BUILD"),
    "Pass --opus-build or set GMOD_VOICE_CAPTURE_OPUS_BUILD")

include(gmcommon .. "/generator.v3.lua")

CreateWorkspace({
    name = "gmod_voice_capture",
    abi_compatible = false
})

CreateProject({
    serverside = true
})

IncludeSDKCommon()
IncludeDetouring()
IncludeScanning()
IncludeLuaShared()

includedirs({
    opus .. "/include",
    gmcommon .. "/helpers_extended/include",
    gmcommon .. "/sourcesdk-minimal/public/tier1"
})

filter({"system:windows", "platforms:x86_64"})
    libdirs({opusBuild .. "/Release"})
    links({"opus"})

filter({"system:linux", "platforms:x86_64"})
    libdirs({opusBuild})
    links({"opus"})

filter({})
