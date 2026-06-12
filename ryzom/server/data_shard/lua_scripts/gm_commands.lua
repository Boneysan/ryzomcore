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

function on_gm_command(command, payload)
	local handler = handlers[command]
	if handler then
		handler(payload)
	else
		egs.warning(string.format("no Lua handler for GM command '%s'", command))
	end
end

egs.info("gm_commands.lua loaded")
