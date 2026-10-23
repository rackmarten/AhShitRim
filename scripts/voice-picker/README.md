# CJ's voice picker

CJ only ever says his own San Andreas lines. This folder builds a local page for listening to every line in his 20 voice banks, reading what each one says, and picking which ones AhShitRim plays where. Everything it reads or writes from the game (the WAVs, the page, the transcripts) lands in `extracted/voice-picker/`, which git ignores: Rockstar's audio and text never get committed. Only the picks (bank and sound ids) go into `protocol/cpp/voice_lines.h`.

## Steps

1. **Extract** the lines from your own San Andreas install (it reads `SaPath` from `local.props`, or takes the folder as an argument):

   ```bash
   node scripts/voice-picker/extract.mjs
   ```

   It writes about 4,100 WAVs to `extracted/voice-picker/wav/` and the page to `extracted/voice-picker/index.html`. Rerun it after changing `picker.html`.

2. **Transcribe** them (optional, but it makes the page searchable). It needs [uv](https://docs.astral.sh/uv/) and an NVIDIA GPU with a current driver:

   ```bash
   uv run scripts/voice-picker/transcribe.py
   ```

   uv builds a private environment for the script (faster-whisper with CUDA 12's cuBLAS and cuDNN as pip wheels; nothing is installed globally or into the system), and the Whisper model (large-v3, about 3 GB) downloads once into the Hugging Face cache. It writes `extracted/voice-picker/transcripts.json` (bank, sound id, voice, speech context, text, confidence per line) and `transcripts.js`, which the page loads. It saves as it goes and skips lines it already has, so an interrupted run just carries on; `--redo` starts over, `--cpu` runs without a GPU (much slower), `--model distil-large-v3` trades some accuracy for speed. On an RTX 4070 the whole set (about 4,100 lines) takes 20 minutes or so, about 3 to 4 lines a second; a rerun with nothing new takes seconds.

   Whisper's text is a guess. Short grunts and shouts often come out as something else, so the page greys out lines it wasn't sure of (low confidence, or likely no speech). Listen before you pick.

3. **Pick.** Open `extracted/voice-picker/index.html` in a browser. The search box matches every word you type against what CJ says, the speech context and the voice (`homie`, `crash`, `AG2`). Press ▶ to listen, and mark the lines you want:

   - **X** and **MMB**: the insult and swear keys.
   - The **talk** menu: CJ's answers in Skyrim's conversations, by category (a line goes in one).
   - **+**: the shout column chosen in "Picking for shouts" above the list, one per shout and word count (Unrelenting Force with 1, 2 or 3 words, and so on, from `protocol/cpp/thuum.h`), plus **any shout without lines of its own**. That's what CJ yells instead of the dragon words. A shout with no picks for the words charged uses its picks for the nearest word count, then the generic pool, which holds his angry fight lines until you pick some. The chooser shows how many each column has, and each line lists the shout columns it's in.

   Picks are kept in that browser; **Download voice-picks.json** when you're done.

4. **Apply** the picks and rebuild:

   ```bash
   node scripts/voice-picker/apply.mjs
   ```

   It reads `voice-picks.json` from your Downloads folder (or a path you pass) and writes the picks into `protocol/cpp/voice_lines.h`. Blocks the file has no picks for stay as they are; `--defaults` resets the conversation and shout blocks to their defaults. Commit that header: it only holds ids.

## Files

- `extract.mjs`: reads `audio/SFX/SPC_PA` through `audio/CONFIG/BankLkup.dat` and writes the WAVs and the page.
- `transcribe.py`: the Whisper pass (a uv script; its dependencies are in its header).
- `picker.html`: the page template; `extract.mjs` fills in the line table and the shouts. Its `groups` list holds every column a line can be picked for (keys, conversation, shouts); a new kind of pick adds a group there and its arrays in `apply.mjs`.
- `apply.mjs`: writes downloaded picks into `voice_lines.h`.
- `ply-speech.json`: which sounds of each bank belong to which of San Andreas's speech contexts (from gta-reversed's `gPlySpeechLookup`).
