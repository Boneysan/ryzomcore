-- gm_commands.lua — GM command handlers (Phase 4.4 / 4.5).
--
-- The EGS NATS subscriber forwards every gm.* command (except script_run and
-- script_reload, which are native) to on_gm_command(command, payload_json).
-- Edit this file and reload it live with:
--   POST /admin/reload-script?name=gm_commands   (go-proxy)
--   luaReload gm_commands                        (EGS admin console)

local handlers = {}

function handlers.weather(payload)
	local kind = egs.jsonGet(payload, "type") or "unknown"
	local duration = egs.jsonGet(payload, "duration_s") or "?"
	egs.info(string.format("GM weather change: %s for %ss (cycle %d)", kind, duration, egs.gameCycle()))
end

function handlers.event_trigger(payload)
	local name = egs.jsonGet(payload, "name") or "unnamed"
	local zone = egs.jsonGet(payload, "zone") or "everywhere"
	egs.info(string.format("GM event '%s' triggered in %s", name, zone))
end

function handlers.spawn(payload)
	local entity = egs.jsonGet(payload, "entity") or "unknown"
	egs.info(string.format("GM spawn request: %s (AI service hookup lands in Phase 6)", entity))
end

function handlers.entity_patch(payload)
	egs.info("GM entity patch: " .. payload)
end

function handlers.despawn(payload)
	egs.info("GM despawn: " .. payload)
end

function handlers.quest_choice(payload)
	local option_id = egs.jsonGet(payload, "option_id")
	local char_id = egs.jsonGet(payload, "char_id")
	local dss = package.loaded["dss_scenario_host"]
	if dss then
		local ok, err = dss.make_choice(option_id, char_id)
		if not ok then
			egs.warning("Quest choice failed: " .. tostring(err))
		end
	else
		egs.warning("dss_scenario_host not loaded")
	end
end

function handlers.start_scenario(payload)
	local name = egs.jsonGet(payload, "name")
	local dss = package.loaded["dss_scenario_host"]
	if dss and name then
		local ok, scenario = pcall(require, name)
		if ok and type(scenario) == "table" then
			dss.start_scenario(scenario)
		else
			egs.warning("Could not load scenario: " .. tostring(name))
		end
	elseif not dss then
		egs.warning("dss_scenario_host not loaded")
	end
end

function handlers.fire_event(payload)
	local kind = egs.jsonGet(payload, "kind")
	local target = egs.jsonGet(payload, "target")
	local dss = package.loaded["dss_scenario_host"]
	if dss and kind then
		dss.fire_event({ on = kind, target = target })
	end
end

-- Called directly by the EGS C++ layer when a creature dies.
function on_creature_death(sheet_id)
	local dss = package.loaded["dss_scenario_host"]
	if dss then
		dss.fire_event({ on = "kill", creature = sheet_id })
	end
end

-- Called directly by the EGS C++ layer when a player character dies.
-- Delegates to party_mechanics for co-op respawn logic.
function on_player_death(char_id)
	local pm = package.loaded["party_mechanics"]
	if pm then
		pm.on_player_death(char_id, nil)
	end
end

function handlers.award_skill(payload)
	local char_id = egs.jsonGet(payload, "character_id")
	local skill = egs.jsonGet(payload, "skill")
	local xp = egs.jsonGet(payload, "xp_amount")
	if char_id and skill and xp then
		local cmd = string.format("addXPToSkill %s %s %s", char_id, xp, skill)
		local ok = egs.executeCommand(cmd)
		if ok then
			egs.info(string.format("GM awarded %s XP in %s to %s", xp, skill, char_id))
		else
			egs.warning("GM award_skill failed to execute command: " .. cmd)
		end
	else
		egs.warning("GM award_skill missing parameters")
	end
end

function on_gm_command(command, payload)
	local handler = handlers[command]
	if handler then
		handler(payload)
	else
		egs.warning(string.format("no Lua handler for GM command '%s'", command))
	end
end

egs.info("gm_commands.lua loaded")
