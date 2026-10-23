// Writes the picks downloaded from the voice picker page (voice-picks.json) into
// protocol/cpp/voice_lines.h, between its "picks" markers, and the dialogue categories' picks (CJ's
// answers in Skyrim's conversations) between its "dialogue picks" markers. A category with no picks
// gets its defaults: CJ's calm lines (the CR and CR2 voices) of the San Andreas speech contexts
// nearest it, from ply-speech.json. Rebuild afterwards.
//
// Usage: node scripts/voice-picker/apply.mjs [path/to/voice-picks.json]
//        node scripts/voice-picker/apply.mjs --dialogue-defaults   only the dialogue block, all defaults
// Without a path it reads voice-picks.json from your Downloads folder. A file with no dialogue picks
// leaves the dialogue block as it is.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../..');
const header = path.join(repo, 'protocol', 'cpp', 'voice_lines.h');
const table = JSON.parse(fs.readFileSync(path.join(here, 'ply-speech.json'), 'utf8'));

// Category -> its array in voice_lines.h and the contexts its defaults come from.
const kDialogue = {
    greet: ['kGreet', ['GENERIC_HI_MALE']],
    agree: ['kAgree', ['AGREE_TO_DO_DRIVEBY', 'SOLICIT_GEN_YES']],
    refuse: ['kRefuse', ['SOLICIT_GEN_NO', 'GANGBANG_NO']],
    ask: ['kAsk', ['UH_HUH']],
    threaten: ['kThreaten', ['PULL_GUN']],
    joke: ['kJoke', ['CASINO_WIN']],
    goodbye: ['kGoodbye', ['SHOP_LEAVE']],
};
const kDefaultVoices = ['CR', 'CR2'];

function contextLines(names) {
    const lines = [];
    for (const name of names) {
        const ctx = table.contexts.find(c => c.name === name);
        if (!ctx) throw new Error(`no context ${name} in ply-speech.json`);
        for (const voice of kDefaultVoices) {
            const v = table.voices.indexOf(voice), r = ctx.ranges[v];
            for (let s = r ? r[0] : 1; r && s <= r[1]; s++) lines.push({ bank: table.firstBank + v, sound: s, voice, context: name });
        }
    }
    return lines;
}

function array(name, lines = []) {
    const sorted = [...lines].sort((a, b) => a.bank - b.bank || a.sound - b.sound);
    for (const l of sorted) {
        if (!(l.bank >= 690 && l.bank <= 709 && l.sound >= 0 && l.sound < 400)) throw new Error(`not one of CJ's lines: ${JSON.stringify(l)}`);
    }
    if (!sorted.length) return `    inline constexpr std::array<Line, 0> ${name}{};\n`;
    const rows = sorted.map(l => `        Line{ ${l.bank}, ${l.sound} },  // ${l.voice} ${l.context}`).join('\n');
    return `    inline constexpr std::array ${name}{\n${rows}\n    };\n`;
}

function replaceBlock(text, begin, end, body) {
    const a = text.indexOf(begin), b = text.indexOf(end);
    if (a < 0 || b < a) throw new Error(`no "${begin.trim()}" markers in ${header}`);
    return text.slice(0, a + begin.length) + body + text.slice(b);
}

const defaultsOnly = process.argv.includes('--dialogue-defaults');
const file = process.argv.slice(2).find(a => !a.startsWith('--')) ?? path.join(os.homedir(), 'Downloads', 'voice-picks.json');
const picks = defaultsOnly ? {} : JSON.parse(fs.readFileSync(file, 'utf8'));
let text = fs.readFileSync(header, 'utf8');
if (!defaultsOnly) {
    text = replaceBlock(text, '    // --- picks (scripts/voice-picker/apply.mjs) ---\n', '    // --- end of picks ---\n',
        array('kInsult', picks.insult) + array('kSwear', picks.swear));
    console.log(`${picks.insult?.length ?? 0} insult and ${picks.swear?.length ?? 0} swear lines written`);
}
if (defaultsOnly || Object.keys(kDialogue).some(c => picks[c]?.length)) {
    const body = Object.entries(kDialogue).map(([c, [name, contexts]]) => array(name, picks[c]?.length ? picks[c] : contextLines(contexts))).join('');
    text = replaceBlock(text, '    // --- dialogue picks (scripts/voice-picker/apply.mjs) ---\n', '    // --- end of dialogue picks ---\n', body);
    console.log('dialogue lines written: ' + Object.keys(kDialogue).map(c => `${c} ${picks[c]?.length ? 'picked' : 'defaults'}`).join(', '));
}
fs.writeFileSync(header, text);
console.log(`to ${path.relative(repo, header)}`);
