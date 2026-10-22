// Writes the picks downloaded from the voice picker page (voice-picks.json) into
// protocol/cpp/voice_lines.h, between its "picks" markers. Rebuild afterwards.
//
// Usage: node scripts/voice-picker/apply.mjs [path/to/voice-picks.json]
// Without a path it reads voice-picks.json from your Downloads folder.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const header = path.join(repo, 'protocol', 'cpp', 'voice_lines.h');
const file = process.argv[2] ?? path.join(os.homedir(), 'Downloads', 'voice-picks.json');
const picks = JSON.parse(fs.readFileSync(file, 'utf8'));

function array(name, lines = []) {
    const sorted = [...lines].sort((a, b) => a.bank - b.bank || a.sound - b.sound);
    for (const l of sorted) {
        if (!(l.bank >= 690 && l.bank <= 709 && l.sound >= 0 && l.sound < 400)) throw new Error(`not one of CJ's lines: ${JSON.stringify(l)}`);
    }
    if (!sorted.length) return `    inline constexpr std::array<Line, 0> ${name}{};\n`;
    const rows = sorted.map(l => `        Line{ ${l.bank}, ${l.sound} },  // ${l.voice} ${l.context}`).join('\n');
    return `    inline constexpr std::array ${name}{\n${rows}\n    };\n`;
}

const begin = '    // --- picks (scripts/voice-picker/apply.mjs) ---\n';
const end = '    // --- end of picks ---\n';
const text = fs.readFileSync(header, 'utf8');
const a = text.indexOf(begin), b = text.indexOf(end);
if (a < 0 || b < a) throw new Error(`no picks markers in ${header}`);
fs.writeFileSync(header, text.slice(0, a + begin.length) + array('kInsult', picks.insult) + array('kSwear', picks.swear) + text.slice(b));
console.log(`${picks.insult?.length ?? 0} insult and ${picks.swear?.length ?? 0} swear lines written to ${path.relative(repo, header)}`);
