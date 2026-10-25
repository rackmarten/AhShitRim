# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "faster-whisper>=1.1,<2",
#     "numpy",
#     "nvidia-cublas-cu12>=12.4,<13",
#     "nvidia-cudnn-cu12>=9.1,<10",
# ]
# ///
"""Transcribes CJ's speech lines for the voice picker, on your own GPU.

Runs faster-whisper (large-v3, English) over the WAVs extract.mjs wrote to extracted/voice-picker/wav/
and writes extracted/voice-picker/transcripts.json (and transcripts.js, which the picker page loads
from file://). Each line gets its bank, sound id, voice, speech context, text and a confidence (the
model's mean token probability, 0 to 1). The text is Rockstar's, so it stays in the gitignored
extracted/ folder like the audio.

Rerunnable: lines already in transcripts.json are skipped, and progress is saved as it goes, so an
interrupted run picks up where it stopped. Pass --redo to start over.

Usage: uv run scripts/voice-picker/transcribe.py [--model large-v3] [--cpu] [--redo]
uv builds a private environment for it (faster-whisper plus CUDA 12's cuBLAS and cuDNN); nothing is
installed globally. The model (about 3 GB) downloads once into the Hugging Face cache.
"""

import argparse
import json
import math
import os
import sys
import time
import wave
from pathlib import Path

import numpy as np

here = Path(__file__).resolve().parent
repo = here.parent.parent
out = repo / "extracted" / "voice-picker"
os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")  # Windows without Developer Mode: the cache copies instead


def add_cuda_dlls():
    # The nvidia-* wheels put their DLLs under site-packages/nvidia/*/bin; CTranslate2 loads them
    # by name, so they go on the DLL search path and PATH before faster_whisper is imported.
    try:
        import nvidia
    except ImportError:
        return
    for root in nvidia.__path__:
        for lib in ("cublas", "cudnn", "cuda_nvrtc"):
            for sub in ("bin", "lib"):
                d = Path(root) / lib / sub
                if d.is_dir():
                    if hasattr(os, "add_dll_directory"):
                        os.add_dll_directory(str(d))
                    os.environ["PATH"] = str(d) + os.pathsep + os.environ.get("PATH", "")


def describe(table):
    """bank:sound -> (bank, sound, voice, context) from ply-speech.json's ranges."""
    lines = {}
    for v, voice in enumerate(table["voices"]):
        bank = table["firstBank"] + v
        for ctx in table["contexts"]:
            r = ctx["ranges"][v]
            if not r:
                continue
            for s in range(r[0], r[1] + 1):
                lines.setdefault(f"{bank}:{s}", (bank, s, voice, ctx["name"]))
    return lines


def load(path):
    """A WAV as 16 kHz float samples. extract.mjs writes 16-bit mono at San Andreas's 15 kHz; going up
    to Whisper's 16 kHz needs no filter, so it's read here rather than through PyAV."""
    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    if rate == 16000 or not len(pcm):
        return pcm
    n = int(round(len(pcm) * 16000 / rate))
    return np.interp(np.arange(n) * (rate / 16000), np.arange(len(pcm)), pcm).astype(np.float32)


def save(result):
    text = json.dumps(result, ensure_ascii=False, indent=1)
    for name, body in (("transcripts.json", text), ("transcripts.js", f"window.transcripts = {text};\n")):
        tmp = out / (name + ".tmp")
        tmp.write_text(body, encoding="utf-8", newline="\n")
        tmp.replace(out / name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="large-v3")
    ap.add_argument("--cpu", action="store_true", help="run on the CPU (slow) instead of CUDA")
    ap.add_argument("--redo", action="store_true", help="ignore existing transcripts")
    args = ap.parse_args()

    wavs = out / "wav"
    if not wavs.is_dir():
        sys.exit(f"No {wavs}: run node scripts/voice-picker/extract.mjs first")
    table = json.loads((here / "ply-speech.json").read_text(encoding="utf-8"))
    known = describe(table)

    path = out / "transcripts.json"
    result = {"model": args.model, "lines": {}}
    if path.exists() and not args.redo:
        result = json.loads(path.read_text(encoding="utf-8"))
        result.setdefault("lines", {})
    done = result["lines"]

    def key_of(p):
        bank, sound = p.stem.split("_")
        return f"{bank}:{sound}"

    todo = sorted((p for p in wavs.glob("*.wav") if key_of(p) not in done),
                  key=lambda p: tuple(map(int, p.stem.split("_"))))
    print(f"{len(done)} lines already transcribed, {len(todo)} to go")
    if not todo:
        save(result)
        return

    if not args.cpu:
        add_cuda_dlls()
    from faster_whisper import WhisperModel

    device, compute = ("cpu", "int8") if args.cpu else ("cuda", "float16")
    model = WhisperModel(args.model, device=device, compute_type=compute)
    result["model"] = args.model

    start = time.time()
    for i, p in enumerate(todo, 1):
        key = key_of(p)
        bank, sound = map(int, key.split(":"))
        _, _, voice, context = known.get(key, (bank, sound, table["voices"][bank - table["firstBank"]], "?"))
        # Each clip is one short line: no carried-over prompt (it makes Whisper repeat itself) and
        # no VAD (it cuts off grunts and one-word shouts).
        segments, _ = model.transcribe(load(p), language="en", beam_size=5, condition_on_previous_text=False,
                                       without_timestamps=True, vad_filter=False)
        segments = list(segments)
        text = " ".join(s.text.strip() for s in segments).strip()
        logprob = sum(s.avg_logprob for s in segments) / len(segments) if segments else -10.0
        no_speech = max((s.no_speech_prob for s in segments), default=1.0)
        done[key] = {"bank": bank, "sound": sound, "voice": voice, "context": context, "text": text,
                     "confidence": round(math.exp(logprob), 3), "noSpeech": round(no_speech, 3)}
        if i % 100 == 0 or i == len(todo):
            save(result)
            rate = i / (time.time() - start)
            print(f"{i}/{len(todo)}  {rate:.1f} lines/s, {(len(todo) - i) / rate / 60:.1f} min left", flush=True)
    print(f"Done in {(time.time() - start) / 60:.1f} min: {path}")


if __name__ == "__main__":
    main()
