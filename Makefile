ifeq ($(CFLAGS_DEBUG),)
CFLAGS_DEBUG:= -g -fsanitize=address -Wall -Wextra -pedantic -Wdocumentation -Werror -DSTRAND_USE_BLOCKS
ifneq ($(wildcard /usr/local/include/valgrind/valgrind.h /usr/include/valgrind/valgrind.h),)
CFLAGS_DEBUG:= $(CFLAGS_DEBUG) -DUSE_VALGRIND
endif
endif

CFLAGS_RELEASE?= -O3 -DNDEBUG -DSTRAND_USE_BLOCKS
CFLAGS?= $(CFLAGS_DEBUG)
CCOPT:= -std=gnu99 -fno-omit-frame-pointer -Iinclude $(CFLAGS)
LDOPT:= $(LDFLAGS) -fsanitize=address -lsiphon

OBJ:= build/obj/ctx.o build/obj/strand.o build/obj/defer.o

all: build/bin/run

-include $(OBJ:.o=.o.d)

build/bin/%: build/obj/%.o $(OBJ) | build/bin
	$(CC) $(LDOPT) $^ -o $@

build/obj/%.o: src/%.c Makefile | build/obj
	$(CC) $(CCOPT) -MMD -MT $@ -MF $@.d -c $< -o $@

build/obj build/bin:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean
.PRECIOUS: build/obj/%.o

