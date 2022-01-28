local core = require "datatree.core"
local datatree = {} ; datatree.__index = datatree

local function update_node(self, node, t)
	local keys = self._keys
	local content = self._content
	for k,v in pairs(t) do
		local kid = keys[k]
		local n = node[kid]
		if type(v) == "table" then
			content[n] = n
			update_node(self, n, v)
		else
			content[n] = v
		end
	end
end

function datatree:update(t)
	-- root id is 1
	local root = self._root
	self._content = { [root] = root }
	update_node(self, root, t)
end

local function dumptree(self, n, name, indent)
	local content = self._content
	local c = content[n]
	if c == nil then
		print(indent .. "[" .. name .. "]")
	else
		local children = content[c]
		if children then
			-- subtree
			print(indent .. name)
			indent = indent .. "  "
			local keyid = self._keyid
			for k,v in pairs(children) do
				local keyname = keyid[k]
				dumptree(self, v, keyname, indent)
			end
		else
			print(indent .. name .. " : " .. tostring(c))
		end
	end
end

function datatree:dump()
	dumptree(self, self._root, "ROOT", "")
end

local function debug_dump_pack(self, pack)
	local keyid = self._keyid
	local content = self._content
	local node = self._node
	for _, item in ipairs(pack) do
		local key = item[3]
		local value = item[4]
		local child = item[5]
		if child then
			value = "[" .. child .. "]"
		else
			value = tostring(value)
		end
		print("nodeid:", item[1],
			key,
			"-> " .. item[2],
			value)
	end
end

function datatree:pack()
	local pack = {}
	local content = self._content
	local node = self._node
	local keys = self._keyid
	local index = 1
	local function pack_table(root)
		local sibling = 0
		for keyid,v in pairs(root) do
			local c = content[v]
			if c ~= nil then
				local nodeid = node[v]
				local item = {
					nodeid,
					sibling,
					keys[keyid],
					c,
				}
				pack[index] = item
				sibling = nodeid
				index = index + 1
				if content[c] then
					item[5] = pack_table(c)
				end
			end
		end
		return sibling
	end
	local child = pack_table(self._root)
--	debug_dump_pack(self, pack)
	return core.pack(child, pack)
end

local function autoid(index)
	local id = 0
	local function makeid(t, k)
		id = id + 1
		t[k] = id
		index[id] = k
		return id
	end
	return setmetatable({}, { __index = makeid })
end

local function init()
	local node = {}
	local nodeid = 0
	local autonode = {}
	local function newnode()
		nodeid = nodeid + 1
		local n = setmetatable( {}, autonode )
		node[n] = nodeid
		return n
	end

	function autonode:__index(key)
		local node = newnode()
		self[key] = node
		return node
	end

	local keyid = {}

	return {
		_keys = autoid(keyid),
		_keyid = keyid,
		_node = node,
		_content = {},
		_root = newnode(),
	}
end

local shadow = {}; shadow.__index = shadow

local function decode(self, id)
	local node = self._node
	local nodeid = self._nodeid
	local t = node[id]
	if t == nil then
		t = {}
		node[id] = t
		nodeid[t] = id
	end
	setmetatable(t, self._lazydecode_trigger )
	core.decode(self._ud, t, id)
	setmetatable(t, nil)
	return t
end

local weak_table = { __mode = "kv" }

local function shadow_init()
	local node = setmetatable( {}, weak_table)
	local nodeid = setmetatable( {}, weak_table )
	local lazydecode = {}
	local lazydecode_trigger = {}
	local obj = {
		_node = node,
		_nodeid = nodeid,
		_lazydecode = lazydecode,
		_lazydecode_trigger = lazydecode_trigger,
	}
	function lazydecode_trigger:__newindex(k,v)
		local subid = v[1]
		local subnode = node[subid]
		if subnode == nil then
			v[1] = nil
			subnode = v
			node[subid] = v
			nodeid[v] = subid
			setmetatable(v, lazydecode)
		end
		rawset(self, k, subnode)
		self[k] = subnode
	end
	local function expand(self)
		local subid = assert(nodeid[self], "Invalid node")
		decode(obj, subid)
		setmetatable(self, nil)
	end
	function lazydecode:__index(k)
		expand(self)
		return self[k]
	end
	function lazydecode:__pairs()
		expand(self)
		return next, self
	end

	return obj
end

function shadow:update(ud)
	-- clear all
	local lazydecode = self._lazydecode
	for k,node in pairs(self._node) do
		setmetatable(node, nil)
		for k in pairs(node) do
			node[k] = nil
		end
		setmetatable(node, lazydecode)
	end
	self._ud = ud
	return decode(self, 1)
end

function datatree:shadow()
	return setmetatable( shadow_init(), shadow )
end

return function ()
	return setmetatable( init() , datatree )
end