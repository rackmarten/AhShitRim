#include "model_sender.h"

#include "log.h"

#include <Windows.h>

#include <format>
#include <map>

namespace AhShitRim::ModelSender
{
    namespace
    {
        struct Entry
        {
            std::uint32_t                          id = 0;
            std::vector<std::vector<std::uint8_t>> chunks;
            std::string                            label;
            bool                                   pending = true;  // still to send (again)
        };

        struct State
        {
            std::map<int, Entry> entries;  // sent in key order: CJ first
            int                  sending = -1;  // the key being sent, -1 = none
            std::size_t          next = 0;      // its next chunk
            std::uint64_t        startMs = 0;
            bool                 wasUp = false;
        } state;
    }

    void Set(int a_key, std::uint32_t a_id, std::vector<std::vector<std::uint8_t>> a_chunks, std::string a_label)
    {
        state.entries[a_key] = Entry{ a_id, std::move(a_chunks), std::move(a_label), true };
        if (state.sending == a_key) {
            state.sending = -1;
        }
    }

    void Remove(int a_key)
    {
        state.entries.erase(a_key);
        if (state.sending == a_key) {
            state.sending = -1;
        }
    }

    void Pump()
    {
        auto&      link = GetLink();
        const bool up = link.IsUp();
        if (up && !state.wasUp) {
            for (auto& [key, entry] : state.entries) {
                entry.pending = true;
            }
            state.sending = -1;
        }
        state.wasUp = up;
        if (!up) {
            return;
        }
        for (;;) {
            if (state.sending < 0) {
                auto it = state.entries.begin();
                while (it != state.entries.end() && !it->second.pending) {
                    ++it;
                }
                if (it == state.entries.end()) {
                    return;
                }
                state.sending = it->first;
                state.next = 0;
                state.startMs = ::GetTickCount64();
            }
            auto& entry = state.entries[state.sending];
            while (state.next < entry.chunks.size()) {
                const auto& chunk = entry.chunks[state.next];
                if (!link.Send(Protocol::MessageType::ModelChunk, chunk.data(), static_cast<std::uint32_t>(chunk.size()))) {
                    return;  // the ring is full: the rest next frame
                }
                state.next++;
            }
            entry.pending = false;
            state.sending = -1;
            Log(std::format("{} {:08X} sent to Skyrim in {} chunks ({} ms)", entry.label, entry.id, entry.chunks.size(), ::GetTickCount64() - state.startMs));
        }
    }
}
