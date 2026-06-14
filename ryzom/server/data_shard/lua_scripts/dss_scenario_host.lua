-- dss_scenario_host.lua — Wiring quest_runtime.lua into dynamic_scenario_service
-- Implements PostgreSQL progress persistence and NATS journal broadcast.

local quest_runtime = require("quest_runtime")

local M = {}
M.current_story = nil

-- Helper to encode journal state as JSON
local function encode_journal_json(state)
    local function escape(s)
        if not s then return "" end
        -- Backslash MUST be escaped first, then quotes and control chars,
        -- otherwise text containing '\' or a newline yields invalid JSON.
        s = string.gsub(s, '\\', '\\\\')
        s = string.gsub(s, '"', '\\"')
        s = string.gsub(s, '\n', '\\n')
        s = string.gsub(s, '\r', '\\r')
        s = string.gsub(s, '\t', '\\t')
        return s
    end

    local is_awaiting = state.awaiting_choice and "true" or "false"
    
    local options_json = "null"
    if state.choice_options then
        local opts = {}
        for i, opt in ipairs(state.choice_options) do
            opts[#opts+1] = string.format('{"id":"%s","text":"%s"}', escape(opt.id), escape(opt.text))
        end
        options_json = "[" .. table.concat(opts, ",") .. "]"
    end

    return string.format(
        '{"type":"quest_update","storyline":"%s","active_quest":"%s","quest_name":"%s","objective":"%s","objective_text":"%s","awaiting_choice":%s,"choice_prompt":"%s","choice_npc":"%s","choice_npc_line":"%s","choice_options":%s}',
        escape(state.storyline),
        escape(state.active_quest),
        escape(state.quest_name),
        escape(state.objective),
        escape(state.objective_text),
        is_awaiting,
        escape(state.choice_prompt),
        escape(state.choice_npc),
        escape(state.choice_npc_line),
        options_json
    )
end

local function persist_and_publish()
    if not M.current_story then return end
    local state = M.current_story:state()
    local json_str = encode_journal_json(state)
    
    -- Phase 5.3: Publish side of NATS journal broadcast
    if dss_journalPublish then
        dss_journalPublish(json_str)
    end
    
    -- Phase 5.3: PostgreSQL progress persistence
    if dss_saveProgress then
        dss_saveProgress(M.current_story.id, json_str)
    end
end

-- Host hook triggered on consequences
local function handle_effect(effect)
    egs.info("DSS: Firing effect -> " .. tostring(effect.action))
    
    if effect.action == "faction" and dss_saveFactionStanding then
        -- We need the account_id. For now, we will assume a single-player context 
        -- or retrieve it from the environment if available.
        local account_id = effect.account_id or "0"
        dss_saveFactionStanding(tostring(account_id), tostring(effect.faction), tostring(effect.amount))
    end
    
    -- Other consequence mappings (spawn creature, give item, etc.)
end

-- Load and begin a scenario
function M.start_scenario(storyline)
    M.current_story = storyline
    M.current_story.on_effect = handle_effect
    M.current_story:begin()
    egs.info("DSS: Started storyline " .. tostring(storyline.id))
    persist_and_publish()
end

-- Handle a player choice
function M.make_choice(option_id, account_id)
    if not M.current_story then return false, "no active storyline" end
    if not M.current_story.pending_choice then return false, "no pending choice" end
    
    local quest_id = M.current_story.pending_choice.quest
    local objective_id = M.current_story.pending_choice.objective
    
    local ok, chosen = M.current_story:choose(option_id)
    if ok then
        egs.info("DSS: Choice made: " .. tostring(option_id))
        if dss_saveChronicleChoice then
            dss_saveChronicleChoice(M.current_story.id, quest_id, objective_id, chosen.id, tostring(account_id))
        end
        persist_and_publish()
    end
    return ok, chosen
end

-- Hook for incoming world events
function M.fire_event(ev)
    if M.current_story then
        local advanced = M.current_story:fire(ev)
        if advanced then
            egs.info("DSS: Objective advanced/progressed")
            persist_and_publish()
        end
    end
end

-- The hook called by dynamic_scenario_service::update()
function dss_update()
    -- Hook for ticking time-based objective limits if any
end

egs.info("dss_scenario_host.lua loaded successfully")
return M
