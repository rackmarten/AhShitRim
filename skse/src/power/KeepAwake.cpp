#include "power/KeepAwake.h"

#include <Windows.h>

namespace AhShitRim::KeepAwake
{
    namespace
    {
        HANDLE request = nullptr;
        bool   held = false;
    }

    bool Display(bool a_hold)
    {
        if (a_hold == held) {
            return true;
        }
        if (!request) {
            REASON_CONTEXT reason{};
            reason.Version = POWER_REQUEST_CONTEXT_VERSION;
            reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            reason.Reason.SimpleReasonString = const_cast<LPWSTR>(L"AhShitRim: San Andreas has the player");
            request = ::PowerCreateRequest(&reason);
            if (request == INVALID_HANDLE_VALUE) {
                request = nullptr;
                return false;
            }
        }
        const BOOL ok = a_hold ? ::PowerSetRequest(request, PowerRequestDisplayRequired) : ::PowerClearRequest(request, PowerRequestDisplayRequired);
        if (!ok) {
            return false;
        }
        held = a_hold;
        return true;
    }
}
