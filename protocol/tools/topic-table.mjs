// Builds protocol/cpp/dialogue_topics.h: which of CJ's line categories (greet, agree, refuse, ask,
// threaten, joke, goodbye) answers each Skyrim dialogue topic the player can pick, by topic FormID.
// Reads Skyrim.esm and its English strings (Strings/ in "Skyrim - Interface.bsa") from your own
// install; only FormIDs and categories are written, never Bethesda's text.
//
// Which topics: those of the main quest up to the Greybeards (MQ101 to MQ105) and the generic ones
// every town has (the Dialogue*, Favor* and Rumor quests: innkeepers, merchants, jarls, rumours).
//
// How they're classified, first match wins:
//   1. protocol/tools/topic-hand.json, FormID (hex) -> category: picks made by hand
//   2. with EIGHTBALL_URL and EIGHTBALL_KEY set, a local LLM through 8-ball's OpenAI-compatible API
//      (EIGHTBALL_URL is the API's base, ending in /openai/v1)
//   3. the keyword rule, the same as the plugin's runtime fallback (Dialogue::Classify in
//      protocol/cpp/dialogue.h); keep the two in step. These are left out of the header, since the
//      plugin comes to the same answer on its own
//
// Usage: node protocol/tools/topic-table.mjs ["C:/.../Skyrim Special Edition"]
//        node protocol/tools/topic-table.mjs --list [path]   prints each topic's text and category
//                                                            (to the console only) for hand picks
// Without a path it reads SkyrimPath from local.props. Rebuild afterwards.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../..');
const out = path.join(repo, 'protocol', 'cpp', 'dialogue_topics.h');
const handFile = path.join(here, 'topic-hand.json');

const kCategories = ['greet', 'agree', 'refuse', 'ask', 'threaten', 'joke', 'goodbye'];

// ---- the keyword rule (protocol/cpp/dialogue.h's Classify) ----

const kGoodbye = ['goodbye', 'farewell', 'bye', 'see ya', 'later', 'part ways', 'i should go', 'i have to go', 'i need to go', 'i must go',
    "i'll be going", 'time to go'];
const kRefuseStart = ['no', 'nope', 'nah'];
const kRefuse = ['never mind', 'nevermind', 'not interested', 'forget it', "i won't", 'i will not', "i'm not going", 'not going to', "i don't want",
    'rather not', 'no thanks', 'maybe not', 'not now', 'another time', 'too rich', 'i refuse', "i can't", 'i cannot'];
const kGreetStart = ['hello', 'hi', 'hey', 'greetings', 'good day', 'good morning', 'good evening', 'well met', 'hail'];
const kJoke = ['joke', 'kidding', 'funny', 'haha', 'ha ha'];
const kThreaten = ['kill you', "you'll regret", 'stand aside', 'out of my way', 'shut up', 'rather die', "or i'll", "i'll kill", "you'll pay", 'back off'];
const kAgreeStart = ['yes', 'yeah', 'yep', 'sure', 'okay', 'ok', 'alright', 'all right', 'fine', 'deal', 'very well', 'of course', 'gladly',
    'sounds good', "let's", 'agreed', 'understood', 'got it', 'certainly'];
const kAgree = ["i'll do", "i'll take", "i'll help", "i'll bring", "i'll deliver", "i'll get", "i'll find", "i'll come", "i'll go", "i'll talk",
    "i'll see", "i'll pay", "i'll rent", "i'll hire", "i'd like", 'i want to', 'i accept', 'consider it done', 'count me in', "i'm in", "i'm ready",
    "i'm sure", 'take this', 'here you go', 'i have the', 'i got you', 'i submit', 'follow me', 'wait here', 'maybe i will', 'i will', 'let me'];

// Lower case, words between single spaces with one at either end, apostrophes kept, "(...)" left out.
function words(text) {
    let out = ' ', depth = 0;
    for (const c of text) {
        if (c === '(') depth++;
        else if (c === ')') depth = Math.max(depth - 1, 0);
        else if (depth === 0 && /^[A-Za-z0-9']$/.test(c)) { out += c.toLowerCase(); continue; }
        if (!out.endsWith(' ')) out += ' ';
    }
    return out.endsWith(' ') ? out : out + ' ';
}

function classify(text) {
    const lower = text.toLowerCase(), w = words(text);
    const has = list => list.some(p => w.includes(` ${p} `));
    const starts = list => list.some(p => w.startsWith(` ${p} `));
    if (lower.includes('(intimidate)') || lower.includes('(brawl)') || has(['or else'])) return 'threaten';
    if (has(kGoodbye)) return 'goodbye';
    if (lower.includes('?')) return 'ask';
    if (starts(kRefuseStart) || has(kRefuse)) return 'refuse';
    if (starts(kGreetStart)) return 'greet';
    if (has(kJoke)) return 'joke';
    if (has(kThreaten)) return 'threaten';
    if (starts(kAgreeStart) || has(kAgree)) return 'agree';
    return 'ask';
}

// ---- Skyrim's files ----

function skyrimPath() {
    const arg = process.argv.slice(2).find(a => !a.startsWith('--'));
    if (arg) return arg;
    const props = fs.readFileSync(path.join(repo, 'local.props'), 'utf8');
    const m = props.match(/<SkyrimPath>([^<]+)<\/SkyrimPath>/);
    if (!m) throw new Error('No SkyrimPath in local.props: pass Skyrim\'s folder as the first argument');
    return m[1];
}

// One file out of an uncompressed BSA (version 104 or 105), by its lower-case path.
function readBsa(file, want) {
    const fd = fs.openSync(file, 'r');
    const read = (pos, n) => { const b = Buffer.alloc(n); fs.readSync(fd, b, 0, n, pos); return b; };
    const h = read(0, 36);
    if (h.toString('latin1', 0, 4) !== 'BSA\0') throw new Error(`${file} is not a BSA`);
    const version = h.readUInt32LE(4), flags = h.readUInt32LE(12), folders = h.readUInt32LE(16), files = h.readUInt32LE(20);
    const folderNames = h.readUInt32LE(24), fileNames = h.readUInt32LE(28);
    if ((flags & 3) !== 3) throw new Error(`${file}: no folder or file names`);
    const recordSize = version === 105 ? 24 : 16;
    const folderCounts = [];
    for (let i = 0; i < folders; i++) folderCounts.push(read(36 + i * recordSize + 8, 4).readUInt32LE(0));
    let pos = 36 + folders * recordSize;
    const entries = [];
    for (const count of folderCounts) {
        const len = read(pos, 1)[0];
        const folder = read(pos + 1, len - 1).toString('latin1');
        pos += 1 + len;
        const block = read(pos, count * 16);
        for (let i = 0; i < count; i++) entries.push({ folder, size: block.readUInt32LE(i * 16 + 8), offset: block.readUInt32LE(i * 16 + 12) });
        pos += count * 16;
    }
    const names = read(pos, fileNames).toString('latin1').split('\0');
    const compressedByDefault = (flags & 4) !== 0;
    for (let i = 0; i < files; i++) {
        const e = entries[i];
        if (`${e.folder}\\${names[i]}`.toLowerCase() !== want) continue;
        const compressed = compressedByDefault !== ((e.size & 0x40000000) !== 0);
        if (compressed) throw new Error(`${want} is compressed in ${file}; not supported`);
        const data = read(e.offset, e.size & 0x3FFFFFFF);
        fs.closeSync(fd);
        return data;
    }
    fs.closeSync(fd);
    throw new Error(`${want} not found in ${file}`);
}

// A .STRINGS file: id -> text.
function readStrings(data) {
    const count = data.readUInt32LE(0);
    const base = 8 + count * 8;
    const strings = new Map();
    for (let i = 0; i < count; i++) {
        const id = data.readUInt32LE(8 + i * 8), offset = base + data.readUInt32LE(12 + i * 8);
        strings.set(id, data.toString('utf8', offset, data.indexOf(0, offset)));
    }
    return strings;
}

function subrecords(data) {
    const subs = [];
    for (let pos = 0, big = 0; pos + 6 <= data.length;) {
        const type = data.toString('latin1', pos, pos + 4);
        let size = data.readUInt16LE(pos + 4);
        pos += 6;
        if (type === 'XXXX') { big = data.readUInt32LE(pos); pos += size; continue; }
        if (big) { size = big; big = 0; }
        subs.push({ type, data: data.subarray(pos, pos + size) });
        pos += size;
    }
    return subs;
}

// The QUST, DIAL and INFO records of a plugin, with their subrecords.
function readPlugin(file) {
    const buf = fs.readFileSync(file);
    const records = [];
    const walk = (start, end) => {
        for (let pos = start; pos < end;) {
            const type = buf.toString('latin1', pos, pos + 4);
            const size = buf.readUInt32LE(pos + 4);
            if (type === 'GRUP') {
                const label = buf.toString('latin1', pos + 8, pos + 12), groupType = buf.readInt32LE(pos + 12);
                if (groupType !== 0 || ['QUST', 'DIAL'].includes(label)) walk(pos + 24, pos + size);
                pos += size;
                continue;
            }
            const flags = buf.readUInt32LE(pos + 8), formId = buf.readUInt32LE(pos + 12);
            if (['QUST', 'DIAL', 'INFO'].includes(type)) {
                let data = buf.subarray(pos + 24, pos + 24 + size);
                if (flags & 0x40000) data = zlib.inflateSync(data.subarray(4));
                records.push({ type, formId, flags, subs: subrecords(data) });
            }
            pos += 24 + size;
        }
    };
    walk(0, buf.length);
    return records;
}

// The topics the player can pick: a name, in a quest of the main quest up to the Greybeards or a
// generic one, the Topic category (not combat, detection, scene or service chatter).
function topics(skyrim) {
    const data = path.join(skyrim, 'Data');
    const strings = readStrings(readBsa(path.join(data, 'Skyrim - Interface.bsa'), 'strings\\skyrim_english.strings'));
    const records = readPlugin(path.join(data, 'Skyrim.esm'));
    const sub = (r, t) => r.subs.find(s => s.type === t)?.data;
    const edid = r => sub(r, 'EDID')?.toString('latin1').replace(/\0.*$/s, '') ?? '';
    const quests = new Map(records.filter(r => r.type === 'QUST').map(r => [r.formId, edid(r)]));
    const wanted = q => /^MQ10[1-5]/.test(q) || /^(Dialogue|Favor|Rumor)/.test(q);
    const result = [];
    let topic = null;
    for (const r of records) {
        if (r.type === 'DIAL') {
            topic = null;
            const full = sub(r, 'FULL'), qnam = sub(r, 'QNAM'), dataSub = sub(r, 'DATA');
            const quest = qnam ? quests.get(qnam.readUInt32LE(0)) ?? '' : '';
            const category = dataSub ? dataSub[1] : 0xFF;
            const text = full ? strings.get(full.readUInt32LE(0)) ?? '' : '';
            if (category !== 0 || !text || !wanted(quest) || (r.flags & 0x20)) continue;  // 0 = Topic; 0x20 deleted
            topic = { formId: r.formId, quest, text, prompts: [] };
            result.push(topic);
        } else if (r.type === 'INFO' && topic) {
            const rnam = sub(r, 'RNAM');  // a response's own prompt, shown instead of the topic's
            const prompt = rnam ? strings.get(rnam.readUInt32LE(0)) : '';
            if (prompt && !topic.prompts.includes(prompt)) topic.prompts.push(prompt);
        }
    }
    return result;
}

async function askLlm(items) {
    const url = process.env.EIGHTBALL_URL.replace(/\/$/, '');
    const prompt = `Each line below is something the player character says in a Skyrim conversation. ` +
        `Answer with one word per line, in the same order, the category of what the player says: ` +
        `${kCategories.join(', ')}. "agree" includes accepting a task or handing something over; ` +
        `"ask" is any question or request for information.\n\n` + items.map((t, i) => `${i + 1}. ${t}`).join('\n');
    const res = await fetch(`${url}/chat/completions`, {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${process.env.EIGHTBALL_KEY}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ model: 'text', messages: [{ role: 'user', content: prompt }], temperature: 0 }),
    });
    if (!res.ok) throw new Error(`8-ball answered ${res.status}`);
    const answer = (await res.json()).choices[0].message.content.replace(/<think>[\s\S]*?<\/think>/, '');
    const words = answer.toLowerCase().match(/[a-z]+/g)?.filter(w => kCategories.includes(w)) ?? [];
    return items.map((_, i) => words[i] ?? null);
}

async function main() {
    const list = process.argv.includes('--list');
    const all = topics(skyrimPath());
    const hand = fs.existsSync(handFile) ? JSON.parse(fs.readFileSync(handFile, 'utf8')) : {};
    const byLlm = new Map();
    if (process.env.EIGHTBALL_URL && process.env.EIGHTBALL_KEY) {
        for (let i = 0; i < all.length; i += 40) {
            const batch = all.slice(i, i + 40);
            (await askLlm(batch.map(t => t.prompts[0] ?? t.text))).forEach((c, j) => c && byLlm.set(batch[j].formId, c));
            process.stdout.write(`\r8-ball: ${Math.min(i + 40, all.length)} of ${all.length}`);
        }
        console.log();
    }
    const counts = Object.fromEntries(kCategories.map(c => [c, 0]));
    const source = { hand: 0, llm: 0, rule: 0 };
    let rows = all.map(t => {
        const id = t.formId.toString(16).toUpperCase().padStart(8, '0');
        const how = hand[id] ? 'hand' : byLlm.has(t.formId) ? 'llm' : 'rule';
        const category = hand[id] ?? byLlm.get(t.formId) ?? classify(t.prompts[0] ?? t.text);
        if (!kCategories.includes(category)) throw new Error(`${id}: unknown category ${category}`);
        counts[category]++;
        source[how]++;
        if (list) console.log(`${id} ${category.padEnd(8)} ${how.padEnd(4)} ${t.quest.padEnd(28)} ${t.text}${t.prompts.length ? `  [${t.prompts.join(' | ')}]` : ''}`);
        return { id, category, how };
    });
    if (list) return;
    // The rule's own picks stay out: the plugin falls back to the same rule at runtime.
    rows = rows.filter(r => r.how !== 'rule');
    rows.sort((a, b) => a.id.localeCompare(b.id));
    const body = rows.map(r => `        { 0x${r.id}, Category::${r.category[0].toUpperCase()}${r.category.slice(1)} },`).join('\n');
    fs.writeFileSync(out, `// Generated by protocol/tools/topic-table.mjs from Skyrim.esm: do not edit by hand (hand picks go in
// protocol/tools/topic-hand.json). Skyrim.esm's dialogue topics (DIAL FormIDs) the player can pick in
// the main quest up to the Greybeards and the generic town dialogue, and the category of CJ's line
// that answers each, picked by hand or by 8-ball (the others are left to the runtime rule). IDs only:
// no Bethesda text.

#pragma once

#include <array>
#include <cstdint>

namespace AhShitRim::Dialogue
{
    enum class Category : std::uint8_t;

    struct TopicEntry
    {
        std::uint32_t formId;
        Category      category;
    };

    // Sorted by FormID.
    inline constexpr std::array<TopicEntry, ${rows.length}> kTopics{ {
${body}
    } };
}
`, { encoding: 'utf8' });
    console.log(`${rows.length} topics written to ${path.relative(repo, out)} (${source.hand} by hand, ${source.llm} by 8-ball, ${source.rule} by the rule): ` +
        kCategories.map(c => `${counts[c]} ${c}`).join(', '));
}

await main();
