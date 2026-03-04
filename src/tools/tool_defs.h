#pragma once

#include <string>

namespace rastack {

// Minimal flat tool definitions for Qwen3 0.6B — keep short to avoid overwhelming the small model
static const char* DEFAULT_TOOL_DEFS_JSON = R"([
  {"name": "get_current_time", "description": "Get the current date and time", "parameters": {}},
  {"name": "get_weather", "description": "Get weather for a location", "parameters": {"location": "city name"}},
  {"name": "calculate", "description": "Evaluate a math expression", "parameters": {"expression": "math expression like '2 + 2'"}}
])";

} // namespace rastack
