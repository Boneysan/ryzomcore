-- party_mechanics.lua — Co-op Death and Respawn Mechanics (Phase 5.8-C)

local M = {}

-- party_id → {x, y, z} anchor position
M.party_anchors = {}

-- char_id → party_id membership; populated by register_player / leave_party
M.char_to_party = {}

-- party_id → { char_id = true } membership cache for route lifecycle decisions
M.party_members = {}

-- instance_id → frontend address advertised to the Go proxy
M.instance_frontends = {}

-- party_id → instance_id assigned by GM/AIS orchestration
M.party_instances = {}

-- party_id → frontend address currently registered with the Go proxy
M.party_frontends = {}

function M.set_anchor(party_id, x, y, z)
    M.party_anchors[party_id] = {x=x, y=y, z=z}
    egs.info(string.format("Party %s anchor set to (%.1f, %.1f, %.1f)", party_id, x, y, z))
end

-- Call when a character is assigned to a party (login / GM assign command).
function M.register_player(char_id, party_id)
    local old_party = M.char_to_party[char_id]
    if old_party and M.party_members[old_party] then
        M.party_members[old_party][char_id] = nil
    end

    M.char_to_party[char_id] = party_id
    M.party_members[party_id] = M.party_members[party_id] or {}
    M.party_members[party_id][char_id] = true
    egs.info(string.format("Registered %s → party %s", char_id, party_id))
end

-- Call when a character leaves a party or disconnects.
function M.leave_party(char_id)
    local party_id = M.char_to_party[char_id]
    if party_id and M.party_members[party_id] then
        M.party_members[party_id][char_id] = nil
    end
    M.char_to_party[char_id] = nil
end

-- Direct party route. Passing an empty addr deregisters the party route.
function M.set_party_frontend(party_id, addr)
    if party_id == nil or tostring(party_id) == "" then
        return false, "party_id is required"
    end

    local route_addr = addr and tostring(addr) or ""
    local party_key = tostring(party_id)

    if route_addr == "" then
        M.party_frontends[party_key] = nil
        egs.info("Clearing frontend route for party " .. party_key)
    else
        M.party_frontends[party_key] = route_addr
        egs.info(string.format("Routing party %s to frontend %s", party_key, route_addr))
    end

    egs.registerPartyFrontend(party_key, route_addr)
    return true
end

-- Frontend route for an AI/world instance. Existing parties assigned to that
-- instance are republished immediately so the Go proxy table catches up.
function M.set_instance_frontend(instance_id, addr)
    if instance_id == nil or tostring(instance_id) == "" then
        return false, "instance_id is required"
    end

    local instance_key = tostring(instance_id)
    local route_addr = addr and tostring(addr) or ""

    if route_addr == "" then
        M.instance_frontends[instance_key] = nil
    else
        M.instance_frontends[instance_key] = route_addr
    end

    for party_id, assigned_instance in pairs(M.party_instances) do
        if tostring(assigned_instance) == instance_key then
            M.set_party_frontend(party_id, route_addr)
        end
    end

    return true
end

-- Assign a party to an AI/world instance and publish the route if that
-- instance already has a frontend.
function M.assign_party_instance(party_id, instance_id)
    if party_id == nil or tostring(party_id) == "" then
        return false, "party_id is required"
    end
    if instance_id == nil or tostring(instance_id) == "" then
        return false, "instance_id is required"
    end

    local party_key = tostring(party_id)
    local instance_key = tostring(instance_id)
    M.party_instances[party_key] = instance_key

    local route_addr = M.instance_frontends[instance_key]
    if route_addr then
        return M.set_party_frontend(party_key, route_addr)
    end

    egs.info(string.format("Party %s assigned to instance %s; no frontend registered for that instance yet", party_key, instance_key))
    return true
end

function M.on_character_instance_changed(char_id, instance_id)
    if char_id == nil or tostring(char_id) == "" then
        return false, "char_id is required"
    end

    local char_key = tostring(char_id)
    local party_id = M.char_to_party[char_key]
    if not party_id then
        return true
    end

    local instance_key = instance_id and tostring(instance_id) or ""
    if instance_key == "" or instance_key == "-1" or instance_key == "4294967295" then
        return M.clear_party_instance(party_id)
    end

    return M.assign_party_instance(party_id, instance_key)
end

function M.clear_party_instance(party_id)
    if party_id == nil or tostring(party_id) == "" then
        return false, "party_id is required"
    end

    local party_key = tostring(party_id)
    M.party_instances[party_key] = nil
    M.set_party_frontend(party_key, "")
    return true
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
