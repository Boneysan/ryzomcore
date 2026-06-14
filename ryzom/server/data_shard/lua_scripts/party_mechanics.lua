-- party_mechanics.lua — Co-op Death and Respawn Mechanics (Phase 5.8-C)

local M = {}

-- party_id → {x, y, z} anchor position
M.party_anchors = {}

-- char_id → party_id membership; populated by register_player / leave_party
M.char_to_party = {}

function M.set_anchor(party_id, x, y, z)
    M.party_anchors[party_id] = {x=x, y=y, z=z}
    egs.info(string.format("Party %s anchor set to (%.1f, %.1f, %.1f)", party_id, x, y, z))
end

-- Call when a character is assigned to a party (login / GM assign command).
function M.register_player(char_id, party_id)
    M.char_to_party[char_id] = party_id
    egs.info(string.format("Registered %s → party %s", char_id, party_id))
end

-- Call when a character leaves a party or disconnects.
function M.leave_party(char_id)
    M.char_to_party[char_id] = nil
end

-- Handle death mechanics: revive at anchor if character is in a party with one,
-- otherwise fall through to standard respawn.
function M.on_player_death(char_id)
    egs.info("Player death detected: " .. tostring(char_id))

    local party_id = M.char_to_party[char_id]
    local anchor = party_id and M.party_anchors[party_id]

    if anchor then
        egs.info(string.format("Respawning %s at party %s anchor (%.1f, %.1f, %.1f)",
            char_id, party_id, anchor.x, anchor.y, anchor.z))
        egs.revive(char_id)
        egs.teleport(char_id, anchor.x, anchor.y, anchor.z)
        return true
    end

    egs.info("Player " .. tostring(char_id) .. " using standard respawn.")
    return false
end

-- Trigger stash sync — publishes party.stash.sync so go-proxy can fetch and fan out.
function M.sync_stash(party_id)
    egs.info("Syncing stash for party " .. tostring(party_id))
    local payload = string.format('{"party_id":"%s"}', tostring(party_id))
    egs.natsPublish("party.stash.sync", payload)
end

egs.info("party_mechanics.lua loaded successfully")
return M
