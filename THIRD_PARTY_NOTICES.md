# Third-party notices

## gm_8bit

- Project: https://github.com/Meachamp/gm_8bit
- License: GNU Lesser General Public License 2.1 only
- Reused concepts/code: Steam Voice packet framing, Opus frame sequence handling,
  packet-loss concealment and the `SV_BroadcastVoiceData` detour signature.
- Changes: removed relay, audio effects and recompression; added bounded selective
  sessions, explicit lifecycle, silence preservation, WAV output and Lua polling.

## garrysmod_common

- Project: https://github.com/danielga/garrysmod_common
- Pinned commit: `f77a18d86f780a59ea30e4237016b05b790d4b70`
- License: BSD-3-Clause, with Source SDK components under Valve's Source SDK license.

## Opus

- Project: https://opus-codec.org/
- Version: 1.6.1
- License: 3-clause BSD-style license.
- Built statically into the module from the official release source.

## HolyLib

- Project inspected: https://github.com/RaphaelIT7/gmod-holylib
- It was used only to corroborate current engine behavior and 24 kHz PCM handling.
- No HolyLib source code was copied because no repository-wide license was found.

