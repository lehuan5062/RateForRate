#!/usr/bin/env node
// Minimal MCP (Streamable HTTP) client for the local Unreal editor MCP server.
// The editor must be running with:  -ModelContextProtocolStartServer -ModelContextProtocolPort=8000
//
// UE 5.8 exposes three meta-tools: list_toolsets, describe_toolset {toolset_name}, call_tool {toolset_name, tool_name, arguments}.
//   node Tools/mcp_call.mjs tools                                  -> raw tools/list
//   node Tools/mcp_call.mjs toolsets                               -> list_toolsets
//   node Tools/mcp_call.mjs describe <toolset_name>                -> describe_toolset
//   node Tools/mcp_call.mjs call <toolset_name> <tool_name> '{"arg":1}' -> call_tool
//   node Tools/mcp_call.mjs raw <method> '{"json":"params"}'       -> any JSON-RPC method
//
// Env: UE_MCP_URL (default http://127.0.0.1:8000/mcp), UE_MCP_TIMEOUT_MS (default 600000).
// Only talks to loopback unless UE_MCP_URL says otherwise; the server has no authentication.

const MCP_URL = process.env.UE_MCP_URL || 'http://127.0.0.1:8000/mcp';
const TIMEOUT_MS = Number(process.env.UE_MCP_TIMEOUT_MS || 600000);

let sessionId = null;
let nextId = 1;

async function rpc(method, params, { notify = false } = {}) {
	const body = { jsonrpc: '2.0', method };
	if (params !== undefined) body.params = params;
	if (!notify) body.id = nextId++;

	const headers = { 'Content-Type': 'application/json', Accept: 'application/json, text/event-stream' };
	if (sessionId) headers['Mcp-Session-Id'] = sessionId;

	const res = await fetch(MCP_URL, {
		method: 'POST',
		headers,
		body: JSON.stringify(body),
		signal: AbortSignal.timeout(TIMEOUT_MS),
	});
	const sid = res.headers.get('mcp-session-id');
	if (sid) sessionId = sid;

	const text = await res.text();
	if (notify) return null;
	if (!res.ok) throw new Error(`HTTP ${res.status}: ${text.slice(0, 600)}`);

	const contentType = res.headers.get('content-type') || '';
	let message = null;
	if (contentType.includes('text/event-stream')) {
		for (const line of text.split(/\r?\n/)) {
			if (!line.startsWith('data:')) continue;
			const payload = line.slice(5).trim();
			if (!payload) continue;
			try {
				const parsed = JSON.parse(payload);
				if (parsed.id === body.id) message = parsed;
			} catch { /* ignore non-JSON keepalives */ }
		}
	} else {
		message = JSON.parse(text);
	}
	if (!message) throw new Error(`no JSON-RPC response for ${method}: ${text.slice(0, 600)}`);
	if (message.error) throw new Error(`RPC error: ${JSON.stringify(message.error)}`);
	return message.result;
}

async function connect() {
	const init = await rpc('initialize', {
		protocolVersion: '2025-03-26',
		capabilities: {},
		clientInfo: { name: 'shift-happens-tools', version: '0.1.0' },
	});
	await rpc('notifications/initialized', undefined, { notify: true });
	return init;
}

function parseJsonArg(text, fallback) {
	if (text === undefined) return fallback;
	try { return JSON.parse(text); } catch (e) { throw new Error(`bad JSON argument: ${e.message}`); }
}

async function main() {
	const [cmd, ...rest] = process.argv.slice(2);
	if (!cmd) {
		console.error('usage: mcp_call.mjs tools | toolsets | describe <toolset_name> | call <toolset_name> <tool_name> [json] | raw <method> [json]');
		process.exit(2);
	}
	const init = await connect();
	if (process.env.UE_MCP_VERBOSE) console.error('initialize ->', JSON.stringify(init));

	let result;
	switch (cmd) {
		case 'tools':
			result = await rpc('tools/list', {});
			break;
		case 'toolsets':
			result = await rpc('tools/call', { name: 'list_toolsets', arguments: {} });
			break;
		case 'describe':
			result = await rpc('tools/call', { name: 'describe_toolset', arguments: { toolset_name: rest[0] } });
			break;
		case 'call':
			result = await rpc('tools/call', {
				name: 'call_tool',
				arguments: { toolset_name: rest[0], tool_name: rest[1], arguments: parseJsonArg(rest[2], {}) },
			});
			break;
		case 'raw':
			result = await rpc(rest[0], parseJsonArg(rest[1], {}));
			break;
		default:
			throw new Error(`unknown command: ${cmd}`);
	}
	console.log(JSON.stringify(result, null, 2));
}

main().catch((e) => { console.error(String(e.message || e)); process.exit(1); });
