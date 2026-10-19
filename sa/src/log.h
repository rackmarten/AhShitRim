#pragma once

#include <string_view>

#include "link.h"

namespace AhShitRim
{
    void  Log(std::string_view a_line);  // AhShitRim.log next to the .asi, any thread
    Link& GetLink();
}
