ifeq ($(CFLAGS_DEBUG),)
CFLAGS_DEBUG:= -Wall -Wextra -pedantic -Werror -g -march=native -DSTRAND_USE_BLOCKS
ifneq ($(wildcard /usr/local/include/valgrind/valgrind.h /usr/include/valgrind/valgrind.h),)
CFLAGS_DEBUG:= $(CFLAGS_DEBUG) -DUSE_VALGRIND
endif
endif

CFLAGS_RELEASE?= -O3 -march=native -DNDEBUG -DSTRAND_USE_BLOCKS
CFLAGS?= $(CFLAGS_DEBUG)
CCOPT:= -std=gnu99 -fno-omit-frame-pointer -Iinclude $(CFLAGS)
LDOPT:= $(LDFLAGS)

SRC:= src/strand.c
TEST:= test/strand.c
OBJ:= $(SRC:src/%.c=build/obj/%.o)

all: build/bin/run

test: $(TEST:test/%.c=build/bin/test-%)
	@for t in $^; do ./$$t; done

-include $(OBJ:.o=.o.d)

build/bin/%: build/obj/%.o $(OBJ) | build/bin
	$(CC) $(LDOPT) $^ -o $@

build/obj/%.o: src/%.c Makefile | build/obj
	$(CC) $(CCOPT) -MMD -MT $@ -MF $@.d -c $< -o $@

build/obj/test-%.o: test/%.c Makefile | build/obj
	$(CC) $(CCOPT) -MMD -MT $@ -MF $@.d -c $< -o $@

build/obj build/bin:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all test clean
.PRECIOUS: build/obj/%.o

