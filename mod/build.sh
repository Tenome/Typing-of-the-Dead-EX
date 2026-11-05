#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
MH=third_party/minhook
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$PWD/.zig-cache-global}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$PWD/.zig-cache}"
PYTHON=$(command -v python3 || command -v python)
"$PYTHON" -m ziglang cc -target x86-windows-gnu -O2 -shared -s \
  -I"$MH/include" -Isrc \
  src/dllmain.c src/log.c src/dictionary_hooks.c src/word_pack.c src/reading_units.c src/word_difficulty.c src/unit_costs.c \
  src/glyph_tokens.c src/glyph_raster.c src/glyph_hooks.c src/display_hooks.c src/timing_hooks.c src/overlay_hooks.c src/import_profile.c src/frame_interp.c src/input_hooks.c src/texture_hooks.c src/music_hooks.c src/music_player.c src/language_hooks.c src/model_hooks.c src/cutscene_hooks.c src/cheat_hooks.c src/character_hooks.c src/subtitle_hooks.c src/subtitle_text.c src/subtitle_timing.c src/randomizer.c src/enemy_counts.c src/online_hooks.c src/online_fingerprint.c src/determinism.c src/frame_hooks.c src/memory_watch.c src/mutators.c src/word_mutators.c src/typing_stats.c src/replay.c src/pause_menu.c src/sound_loops.c src/blood.c src/volume.c src/score_overlay.c src/voice_packs.c src/speech.c src/background_mute.c src/paintball.c src/typewriter.c src/achievements.c src/skinned_models.c\
  "$MH/src/buffer.c" "$MH/src/hook.c" "$MH/src/trampoline.c" "$MH/src/hde/hde32.c" \
  src/Common.def -o build/Common.dll -luser32 -lkernel32 -lgdi32 -lwinmm -lgdiplus -lws2_32 -lole32
"$PYTHON" -m ziglang cc -target x86-windows-gnu -O2 -s -Iconfig -Isrc \
  config/config_main.c config/ini_file.c config/pack_builder.c config/pack_coverage.c config/pack_template.c config/game_models.c config/character_page.c config/subtitle_page.c config/randomizer_page.c config/online_page.c config/mutators_page.c config/stats_page.c config/replays_page.c config/audio_page.c src/subtitle_text.c src/subtitle_timing.c config/config.rc \
  src/word_pack.c src/word_mutators.c src/reading_units.c src/word_difficulty.c src/unit_costs.c src/glyph_tokens.c src/log.c \
  -o build/todmod_config.exe -Wl,--subsystem,windows \
  -luser32 -lkernel32 -lgdi32 -lcomctl32 -lshell32 -lcomdlg32 -lgdiplus -lwinmm
