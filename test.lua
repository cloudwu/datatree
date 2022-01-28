local datatree = require "datatree"

local obj = datatree()
local s = obj:shadow()

obj:update {
	a = 1,
	b = 2,
	c = { x = "Hello World" },
}

local copy = s:update(obj:pack())

local copy_c = copy.c

local function get_c_x()
	return copy_c.x
end

print(copy.a, copy.b, get_c_x())

obj:update {
	a = "Hello World",
	b = { 1,2,3 }
}


local copy = s:update(obj:pack())

print(copy.a, copy.b[1])
assert(pcall(get_c_x) == false)

obj:update {
	c = { x = "Update" }
}

local copy = s:update(obj:pack())

print(copy.a)
print(get_c_x())
