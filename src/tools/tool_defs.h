#pragma once

#include <string>

namespace rastack {

static const char* DEFAULT_TOOL_DEFS_JSON = R"([
  {"name": "get_current_time", "description": "Get the current date and time", "parameters": {}},
  {"name": "calculate", "description": "Evaluate a math expression", "parameters": {"expression": "math expression like '2 + 2'"}}
])";

} // namespace rastack
