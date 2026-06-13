-- r2_server_compat.lua — load before any other Ring Lua file on the server
-- Stubs client-only APIs so the shared files load without error
function messageBox(str) print("[R2] " .. tostring(str)) end
function displaySystemInfo(str, _) print("[R2] " .. tostring(str)) end
function colorTag(_, _, _) return "" end
config = config or {}
config.R2EDExtendedDebug = false
