#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REFCONSTANT 0x80000000
#define TYPE_SHIFT 28
#define TYPE_MASK (7 << TYPE_SHIFT)
#define VALUE_MASK ((1 << TYPE_SHIFT) - 1)
#define TYPE_BOOL 1
#define TYPE_INT 2
#define TYPE_REAL 3
#define TYPE_STRING 4
#define TYPE_USERDATA 5
#define TYPE_TABLE 6
#define MAX_VALUE ((1<<TYPE_SHIFT) - 1)
#define MAKE_CONSTANT(type, value) ((type) << TYPE_SHIFT | (value))
#define CHANGE_TYPE(v, t) (((v) & ~TYPE_MASK) | (t) << TYPE_SHIFT)

struct constant_table {
	int index;
	int id;
	size_t sz;
};

static uint32_t
constant(lua_State *L, int idx, struct constant_table *ct) {
	uint32_t c = REFCONSTANT;
	switch (lua_type(L, idx)) {
	case LUA_TNUMBER:
		if (lua_isinteger(L, idx)) {
			lua_Integer n = lua_tointeger(L, idx);
			if (n >= 0 && n <= MAX_VALUE) {
				return MAKE_CONSTANT(TYPE_INT, n);
			}
			c |= TYPE_INT << TYPE_SHIFT;
		} else {
			c |= TYPE_REAL << TYPE_SHIFT;
		}
		break;
	case LUA_TBOOLEAN:
		return MAKE_CONSTANT(TYPE_BOOL, lua_toboolean(L, idx));
	case LUA_TSTRING:
		c |= TYPE_STRING << TYPE_SHIFT;
		break;
	case LUA_TLIGHTUSERDATA:
		c |= TYPE_STRING << TYPE_USERDATA;
		break;
	default:
		luaL_error(L, "Invalid type %s", lua_typename(L, lua_type(L, idx)));
	}
	lua_pushvalue(L, idx);
	if (lua_rawget(L, ct->index) == LUA_TNUMBER) {
		c |= lua_tointeger(L, -1);
		lua_pop(L, 1);
	} else {
		int id = ct->id++;
		if (id > MAX_VALUE) {
			luaL_error(L, "Too many constant");
		}
		c |= id;
		lua_pop(L, 1);
		lua_pushvalue(L, idx);
		lua_pushinteger(L, id);
		lua_rawset(L, ct->index);
		if (lua_type(L, idx) == LUA_TSTRING) {
			size_t sz;
			lua_tolstring(L, idx, &sz);
			ct->sz += sz + 1;
		}
	}
	return c;
}

static uint32_t
get_constant(lua_State *L, int idx, struct constant_table *ct) {
	lua_rawgeti(L, -1, idx);
	uint32_t c = constant(L, -1, ct);
	lua_pop(L, 1);
	return c;
}

static uint32_t
get_int(lua_State *L, int index) {
	if (lua_rawgeti(L, -1, index) != LUA_TNUMBER || !lua_isinteger(L, -1)) {
		luaL_error(L, "Invalid type %s, need integer", lua_typename(L, lua_type(L, -1)));
	}
	uint32_t v = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return v;
}

struct item {
	uint32_t sibling;
	uint32_t key;
	uint32_t value;
};

union constant {
	int64_t n;
	double f;
	void *p;
	const char *s;
};

struct datatree {
	size_t size;
	uint32_t maxid;
	uint32_t maxconstant;
	// union constant[]
	// struct item[]
	// byte stringtable[]
};

static inline union constant *
datatree_constant(const struct datatree *t, uint32_t k) {
	union constant *c = (union constant *)(t+1);
	return &c[k];
}

static inline struct item *
datatree_item(const struct datatree *t, uint32_t index) {
	struct item * item = (struct item *)datatree_constant(t, t->maxconstant);
	return &item[index];
}

static inline char *
datatree_stringbuffer(const struct datatree *t) {
	return (char *)datatree_item(t, t->maxid);
}

static inline const char *
datatree_string(const struct datatree *t, uint32_t index) {
	const char * stringtable = datatree_stringbuffer(t);
	uintptr_t offset = datatree_constant(t, index)->n;
	return &stringtable[offset];
}

static inline int
datatree_type(uint32_t k) {
	return (k & TYPE_MASK) >> TYPE_SHIFT;
}

static union constant
datatree_value(const struct datatree *t, uint32_t k) {
	uint32_t v = k & VALUE_MASK;
	union constant c = { 0 };
	int type = datatree_type(k);
	if (k & REFCONSTANT) {
		if (type == TYPE_STRING) {
			c.s = datatree_string(t, v);
		} else {
			c = *datatree_constant(t, v);
		}
	}
	switch (type) {
	case TYPE_BOOL:
	case TYPE_INT:
	case TYPE_TABLE:
		c.n = v;
		break;
	}
	return c;
}

static void
make_constant_table(lua_State *L, struct constant_table *ct, struct datatree *dt) {
	char * buffer = datatree_stringbuffer(dt);
	size_t offset = 0;
	lua_pushnil(L);
	while (lua_next(L, ct->index) != 0) {
		uint32_t id = lua_tointeger(L, -1);
		union constant *c = datatree_constant(dt, id);
		switch (lua_type(L, -2)) {
		case LUA_TNUMBER:
			if (lua_isinteger(L, -2)) {
				c->n = lua_tointeger(L, -2);
			} else {
				c->f = lua_tonumber(L, -2);
			}
			break;
		case LUA_TSTRING: {
			size_t sz;
			const char * s = lua_tolstring(L, -2, &sz);
			sz += 1;
			memcpy(buffer, s, sz);
			c->n = offset;
			offset += sz;
			buffer += sz;
			break; }
		case LUA_TLIGHTUSERDATA:
			c->p = lua_touserdata(L, -2);
			break;
		default:
			luaL_error(L, "Invalid constant type %s", lua_typename(L, lua_type(L, -2)));
		}
		lua_pop(L, 1);
	}
}

static void
print_value(struct datatree *dt, uint32_t v) {
	union constant c = datatree_value(dt, v);
	switch (datatree_type(v)) {
	case TYPE_BOOL:
		printf("%s", c.n ? "true" : "false");
		break;
	case TYPE_INT:
		printf("%d", (int)c.n);
		break;
	case TYPE_REAL:
		printf("%f", c.f);
		break;
	case TYPE_STRING:
		printf("%s", c.s);
		break;
	case TYPE_USERDATA:
		printf("%p", c.p);
		break;
	case TYPE_TABLE:
		printf("[%d]", (int)c.n);
		break;
	default:
		printf("[Invalid]");
	}
}

static int
ldump(lua_State *L) {
	luaL_checktype(L, 1, LUA_TUSERDATA);
	struct datatree *dt = (struct datatree *)lua_touserdata(L, 1);
	size_t sz = lua_rawlen(L, 1);
	if (sz < sizeof(struct datatree) || dt->size != sz)
		return luaL_error(L, "Invalid datatree");
	int i;
	printf("ROOT");
	print_value(dt, datatree_item(dt, 0)->value);
	printf("\n");
	for (i=1;i<dt->maxid;i++) {
		struct item * item = datatree_item(dt, i);
		if (item->value != 0) {
			printf("[%d] -> %d ", i+1, item->sibling);
			print_value(dt, item->key);
			printf(" : ");
			print_value(dt, item->value);
			printf("\n");
		}
	}
	return 0;
}

static int
lpack(lua_State *L) {
	uint32_t first_child = luaL_checkinteger(L, 1);
	if (first_child >= MAX_VALUE) {
		return luaL_error(L, "Too many items");
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	int n = lua_rawlen(L, 2);
	lua_newtable(L);
	struct constant_table ct = {
		lua_gettop(L),
		0,
		0,
	};
	int i;
	int maxid = 0;
	for (i=1;i<=n;i++) {
		if (lua_rawgeti(L, 2, i) != LUA_TTABLE) {
			return luaL_error(L, "Invalid item %d", i);
		}
		uint32_t nodeid = get_int(L, 1);
		if (nodeid > maxid)
			maxid = nodeid;
		if (nodeid <= 1) {
			return luaL_error(L, "Invalid node id %d", (int)nodeid);
		}
		get_constant(L, 3, &ct);	// key
		if (lua_rawgeti(L, -1, 5) == LUA_TNIL) {
			lua_pop(L, 1);
			get_constant(L, 4, &ct); // value
		} else {
			lua_pop(L, 1);
			get_constant(L, 5, &ct);	// subtable
		}
		lua_pop(L, 1);
	}
	maxid = maxid + 1;
	size_t sz = sizeof(struct datatree) + maxid * sizeof(struct item) + ct.id * sizeof(union constant) + ct.sz;
	struct datatree *dt = (struct datatree *)lua_newuserdatauv(L, sz, 0);
	dt->size = sz;
	dt->maxid = maxid;
	dt->maxconstant = ct.id;
	struct item * root = datatree_item(dt, 0);
	memset(root, 0, sizeof(struct item) * maxid);
	root->sibling = 0;
	root->key = 0;
	root->value = MAKE_CONSTANT(TYPE_TABLE, first_child);
	for (i=1;i<=n;i++) {
		lua_rawgeti(L, 2, i);
		uint32_t nodeid = get_int(L, 1);
		struct item * item = datatree_item(dt, nodeid - 1);
		item->sibling = get_int(L, 2);
		item->key = get_constant(L, 3, &ct);
		if (lua_rawgeti(L, -1, 5) == LUA_TNIL) {
			lua_pop(L, 1);
			item->value = get_constant(L, 4, &ct); // value
		} else {
			lua_pop(L, 1);
			item->value = CHANGE_TYPE(get_constant(L, 5, &ct), TYPE_TABLE);	// subtable
		}
		lua_pop(L, 1);
	}

	make_constant_table(L, &ct, dt);
	return 1;
}

static int
push_value(lua_State *L, const struct datatree *dt, uint32_t v) {
	union constant c = datatree_value(dt, v);
	int t = datatree_type(v);
	switch (t) {
	case TYPE_BOOL:
		lua_pushboolean(L, c.n);
		break;
	case TYPE_INT:
		lua_pushinteger(L, c.n);
		break;
	case TYPE_REAL:
		lua_pushnumber(L, c.f);
		break;
	case TYPE_USERDATA:
		lua_pushlightuserdata(L, c.p);
		break;
	case TYPE_STRING:
		lua_pushstring(L, c.s);
		break;
	case TYPE_TABLE:
		break;
	default:
		luaL_error(L, "Invalid datatree value type");
	}
	return t;
}

static int
ldecode(lua_State *L) {
	const struct datatree *dt = NULL;
	switch (lua_type(L, 1)) {
	case LUA_TSTRING: {
		size_t sz;
		dt = (const struct datatree *)lua_tolstring(L, 1, &sz);
		if (sz < sizeof(struct datatree) || dt->size != sz) {
			return luaL_error(L, "Invalid datatree size (string)");
		}
		break; }
	case LUA_TUSERDATA: {
		size_t sz = lua_rawlen(L, 1);
		dt = (const struct datatree *)lua_touserdata(L, 1);
		if (sz < sizeof(struct datatree) || dt->size != sz) {
			return luaL_error(L, "Invalid datatree size (userdata)");
		}
		break; }
	case LUA_TLIGHTUSERDATA:
		dt = (const struct datatree *)lua_touserdata(L, 1);
		break;
	default:
		return luaL_error(L, "Invalid datatree type (%s)", lua_typename(L, lua_type(L, 1)));
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	uint32_t id = luaL_checkinteger(L, 3);
	if (id == 0 || id >= dt->maxid)
		return luaL_error(L, "Invalid node id (%d)", id);
	struct item * item = datatree_item(dt, id - 1);
	if (datatree_type(item->value) != TYPE_TABLE) {
		return luaL_error(L, "Node (%d) is not a table", id);
	}
	id = datatree_value(dt, item->value).n;
	while (id) {
		item = datatree_item(dt, id - 1);
		push_value(L, dt, item->key);
		if (push_value(L, dt, item->value) == TYPE_TABLE) {
			lua_createtable(L, 1, 0);
			lua_pushinteger(L, id);
			lua_rawseti(L, -2, 1);
			// trigger metamethod
			lua_settable(L, 2);
		} else {
			lua_rawset(L, 2);
		}
		id = item->sibling;
	}
	return 0;
}

LUAMOD_API int
luaopen_datatree_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "pack", lpack },
		{ "dump", ldump },
		{ "decode", ldecode },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}
