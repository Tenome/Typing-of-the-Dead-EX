The Typing of the Dead EX - Modern Community Patch
==================================================

An unofficial, fan-made patch for the Japanese PC version of The Typing of the Dead EX.
It does not change any game file: it adds a small Common.dll that loads the patch and passes everything else on
to the game's original Common.dll. Remove it at any time with uninstall.bat.

You need your own copy of the game. This patch contains no game files.

Install
-------
1. Extract everything into the game folder (the folder with Tod.exe).
2. Run install.bat.
3. Run todmod_config.exe, choose your settings, then "Save and play".

Supported game build: Tod.exe dated 2 June 2011 ("Ver0.500"). With any other build the patch stays inactive and
the game runs unchanged (see todmod.log).

What it adds
------------
- Word packs: your own word lists in any language, mixed by priority, with the original scoring rules.
  "New pack from word list..." in todmod_config.exe turns a plain text file into a pack. See dict\README.txt.
- Words built into the game (drills, single letters, staff names) can be replaced by packs too.
- Optionally mix the words of all packs in use instead of taking each list from the top pack.
- Optionally type a text in order (a play, a script): the top pack's lines come one after another, while
  letters and short-word slots stay as they are (dict\README.txt, "Words in file order").
- Typing follows your Windows keyboard layout, so ' " : and other symbols are where your keyboard says, or a
  chosen layout (US QWERTY, Dvorak, Colemak, Workman) whatever Windows is set to.
- Optional: capital letters must be typed with Shift, with extra points for capitals.
- English mode: the game's own English subtitles instead of the Japanese ones (the voices are English).
- Subtitle editor (todmod_config.exe, "Subtitles" tab): translate the cutscene subtitles into any language.
  Japanese and other non-Latin lines are drawn horizontally at the bottom (subtitles\README.txt), and any
  Windows font can be used for all subtitles. With the English version, tools\english_subtitles.py restores the
  English subtitles of the attract intro, which EX lacks.
- Music packs: WAV, MP3, OGG or FLAC files replace the background music, with LOOPSTART/LOOPEND loop points
  (music\README.txt).
- Backspace cancels the word you are typing, like Esc. Optionally, spaces in pack words can be skipped.
- Word packs can carry a description (description.txt), shown in todmod_config.exe.
- Texture packs: replace any game texture with a PNG, also at higher resolution (textures\README.txt). tools\english_textures.py turns
  the English version's translated menus into a pack.
- Model packs: replace any 3D model with an OBJ file (models\README.txt). Loaded models can be saved as OBJ and
  in the game's own format; tools\model_tool.py exports every model with its textures.
- House of the Dead zombies: with your own copy of The House of the Dead (PC), tools\hotd\make_zombie_pack.py
  turns its zombies into a model pack, make_human_pack.py its people (agents, Sophie, scientists); pick
  "hotd_zombies" and "hotd_humans" under Model packs, then see and choose them on the Characters tab
  (models\README.txt).
- Letters the game font lacks (accents, Greek, Cyrillic, Korean ...) drawn in the game's style, and narrow
  Latin text so longer English sentences fit.
- Any resolution and widescreen: wider 3D view, menus centred, cutscene letterbox widened.
- Extras: live WPM/accuracy/streak, words read aloud (Windows text-to-speech), typewriter gun, photo mode (F12,
  no HUD), mute in the background, hide the reading line, field-of-view slider, paintball blood.
- Achievements (17, shown in game and on the Stats tab) and a practice pack made from your most-missed words.
- Subtitle size and position.
- Subtitle timing: set when each line appears and disappears (Subtitles tab, Start/End, Play voice); subtitles
  now stay up until their voice line ends (a few vanished early).
- Blood colour choice: green, red, purple, gold, blue, grey, black, white, pink, orange, cyan, yellow or rainbow.
- Volume sliders for music, effects and voices (the game only had on/off).
- Voice packs: replace any voice line or sound effect with WAV, OGG or FLAC files (voices\README.txt).
- Score shown in the top right corner during play (Settings, Extras).
- "Import a character model" button: OBJ, glTF or GLB to a model pack without the command line (needs Python).
- Word packs with kana or kanji (e.g. Typing Space Harrier) no longer overlap in English mode.
- Wider configurator with a scrollable word pack description.
- Ending credits and Staff Credits work with word packs again (they stopped at "PRODUCER").
- Infinite lives cheat (Settings, Cheats group).
- Randomizer: three more zombie types that broke when their look was swapped now keep it.
- Rigged characters (glTF/GLB) bend at their own joints: tools\gltf_to_character.py.
- Randomizer: extra copies only for zombies that walk in or throw (a scripted zombie could lose its word).
- Imported characters (tools\obj_to_character.py) stay one smooth mesh that bends at the joints.
- "Player 1/2 as" characters no longer turn invisible when the randomizer shuffles looks.
- Red blood option (Settings, "Red blood"), as in The House of the Dead 2.
- Audio tab in todmod_config.exe: listen to every music track, voice line (with its subtitle) and sound effect.
- Long phrases (more than 24 letters) no longer overlap in English mode.
- Staff Credits (Extra mode) works again with "Player 1/2 as" characters chosen.
- Strength's chainsaw plays its start-up once and loops only the motor (Dreamcast loop points).
- New tool: tools\obj_to_character.py turns a single-mesh character (one OBJ) into a model pack.
- Pause menu: Esc during play (Resume, Restart stage, Exit to menu).
- Randomizer: each zombie may appear 0-4 times, zombies drawn as people; fixed a stall at the Santa Lucia wall
  (axe throwers with another body never threw) and an invisible stage 1 boss.
- Word pack tool for owners of Typing Space Harrier (todmod\tools\tsh_to_word_list.py).
- Mutators (one life, typos cost a life, hardest words, reversed/scrambled words, random case, game speed),
  borderless window, typing statistics (stats\, "Stats" tab) and replays of every game (replays\, "Replays"
  tab); see todmod\gameplay_options.txt.
- Experimental online play (todmod_config.exe, "Online" tab; guide todmod\online_play.txt): two PCs play Arcade
  together, host = player 1, joining PC = player 2, directly, over a VPN or through tools\online_relay.py.
  Tested with two copies on one PC (play_two_copies.bat), not yet between two PCs.
- Randomizer (todmod_config.exe, "Randomizer" tab): zombies and people swap looks, scenes speak other lines
  with matching subtitles, items have random effects; a seed replays the same mix.
- Optional automatic cutscene skipping ("Skip cutscenes automatically").
- Cheats: big heads for everyone (Balloon Head), players always in a zombie or old man/woman skin suit, and
  "Player 1/2 as" any of the game's characters (zombies, civilians, story characters, bosses; a few unusual
  bodies may be invisible). They keep the Dreamcast, battery pack and keyboard. The configurator's "Characters" tab shows a turnable preview of each one, drawn
  from your own game files.
- Low-CPU frame limiter, and optional smooth in-between frames on monitors faster than 60 Hz.
- Runs on Windows' own DirectDraw/Direct3D 7; dgVoodoo is not required (it still works if you use it).
- todmod_config.exe replaces the original Configurater for all of this and the game's own options.

The English version's word lists
--------------------------------
If you own the English Typing of the Dead, tools\english_to_pack.py (Python 3) converts its word folder into a
pack for this game:
    python tools\english_to_pack.py "<English game>\word" "dict\english"

Quitting the game: Alt+F4, then confirm.

Credits
-------
MinHook by Tsuda Kageyu (BSD 2-clause licence, see todmod\MinHook-LICENSE.txt).
