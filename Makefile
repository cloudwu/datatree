LUA_INC=-I /usr/local/include
LUA_LIB=-L /usr/local/bin -llua54

CFLAGS=-O2 -Wall
SHARED=--shared

datatree.dll : datatree.c
	gcc $(CFLAGS) $(SHARED) -o $@ $^ $(LUA_INC) $(LUA_LIB)

clean :
	rm -f datatree.dll

