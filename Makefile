CFLAGS?= -g -std=c99
CCOPT:= -Wall -Wextra -Werror -pedantic -Iinclude $(CFLAGS)
LDOPT:= $(LDFLAGS) -lsiphon

OBJ:= build/obj/context.o build/obj/strand.o build/obj/defer.o

all: build/bin/run

-include $(OBJ:.o=.o.d)

build/bin/%: build/obj/%.o $(OBJ) | build/bin
	$(CC) $(LDOPT) $^ -o $@

build/obj/%.o: src/%.c Makefile | build/obj
	$(CC) $(CCOPT) -MMD -MT $@ -MF $@.d -c $< -o $@

build/obj/%.o: src/%.S Makefile | build/obj
	$(CC) $(CCOPT) -MMD -MT $@ -MF $@.d -c $< -o $@

build/obj build/bin:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean
.PRECIOUS:
