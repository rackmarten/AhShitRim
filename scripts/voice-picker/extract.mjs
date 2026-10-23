// Builds a local page for listening to CJ's speech lines and picking the ones the insult (X) and
// swear (middle mouse) keys play, and his answers in Skyrim's conversations by category. Reads San Andreas's own speech banks from your install
// (audio/SFX/SPC_PA, found through audio/CONFIG/BankLkup.dat) and writes every line of CJ's 20 voice
// banks as a WAV under extracted/voice-picker/, which git ignores: the audio never leaves your machine.
//
// Usage: node scripts/voice-picker/extract.mjs ["C:/Games/Grand Theft Auto San Andreas"]
// Without a path it reads SaPath from local.props. Then open extracted/voice-picker/index.html.
// uv run scripts/voice-picker/transcribe.py adds what each line says, for the page's search (README.md).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../..');
const out = path.join(repo, 'extracted', 'voice-picker');

function saPath() {
    if (process.argv[2]) return process.argv[2];
    const props = fs.readFileSync(path.join(repo, 'local.props'), 'utf8');
    const m = props.match(/<SaPath>([^<]+)<\/SaPath>/);
    if (!m) throw new Error('No SaPath in local.props: pass San Andreas\'s folder as the first argument');
    return m[1];
}

const kLookupSize = 12;     // AEBankLookup: u8 pak, pad, u32 offset, u32 bytes
const kHeaderSize = 4804;   // AEAudioStream: i16 sounds, pad, 400 CAEBankSlotItem of 12 bytes
const table = JSON.parse(fs.readFileSync(path.join(here, 'ply-speech.json'), 'utf8'));

function wav(pcm, rate) {
    const h = Buffer.alloc(44);
    h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVE', 8);
    h.write('fmt ', 12); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22);
    h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
    h.write('data', 36); h.writeUInt32LE(pcm.length, 40);
    return Buffer.concat([h, pcm]);
}

const audio = path.join(saPath(), 'audio');
const lookup = fs.readFileSync(path.join(audio, 'CONFIG', 'BankLkup.dat'));
const fd = fs.openSync(path.join(audio, 'SFX', 'SPC_PA'), 'r');
fs.mkdirSync(path.join(out, 'wav'), { recursive: true });

const seconds = {};  // "bank:sound" -> length
table.voices.forEach((voice, v) => {
    const bank = table.firstBank + v;
    const offset = lookup.readUInt32LE(bank * kLookupSize + 4);
    const bytes = lookup.readUInt32LE(bank * kLookupSize + 8);
    const data = Buffer.alloc(bytes);
    fs.readSync(fd, data, 0, bytes, offset);
    const count = data.readInt16LE(0);
    for (let s = 0; s < count; s++) {
        const item = 4 + s * 12;
        const start = data.readUInt32LE(item);
        const end = s + 1 < count ? data.readUInt32LE(item + 12) : bytes - kHeaderSize;
        const rate = data.readUInt16LE(item + 8);
        const pcm = data.subarray(kHeaderSize + start, kHeaderSize + end);
        fs.writeFileSync(path.join(out, 'wav', `${bank}_${s}.wav`), wav(pcm, rate));
        seconds[`${bank}:${s}`] = Math.round((pcm.length / 2 / rate) * 10) / 10;
    }
    console.log(`${voice}: bank ${bank}, ${count} lines`);
});
fs.closeSync(fd);

const page = fs.readFileSync(path.join(here, 'picker.html'), 'utf8')
    .replace('/*DATA*/null', JSON.stringify({ ...table, seconds }));
fs.writeFileSync(path.join(out, 'index.html'), page);
console.log(`Open ${path.join(out, 'index.html')}`);
