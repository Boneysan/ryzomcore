-- party_mechanics.lua — Co-op Death and Respawn Mechanics (Phase 5.8-C)

local M = {}

-- Store party anchors (mapped by party_id)
M.party_anchors = {}

-- Set an anchor location for the party
function M.set_anchor(party_id, x, y, z)
    M.party_anchors[party_id] = {x=x, y=y, z=z}
    egs.info(string.format("Party %s anchor set to (%.1f, %.1f, %.1f)", party_id, x, y, z))
end

-- Handle death mechanics: if in party, warp to anchor, else normal behavior
function M.on_player_death(char_id, party_id)
    egs.info("Player death detected: " .. tostring(char_id))
    
    if party_id and M.party_anchors[party_id] then
        local anchor = M.party_anchors[party_id]
        egs.info(string.format("Respawning player %s at party anchor %s", char_id, party_id))
        -- In a full implementation, call native C++ respawn bindings here:
        -- egs.teleport(char_id, anchor.x, anchor.y, anchor.z)
        return true -- indicating custom respawn handled
    end
    
    egs.info("Player " .. tostring(char_id) .. " using standard respawn.")
    return false -- fallback to standard respawn
end

-- Trigger stash sync (Lua -> Go)
function M.sync_stash(party_id)
    egs.info("Syncing stash for party " .. tostring(party_id))
    -- In a full implementation, dispatch a NATS message or HTTP request
    -- to campaign-api POST /party/{party_id}/stash
end

egs.info("party_mechanics.lua loaded successfully")
return M
