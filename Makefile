CFLAGS_DEBUG:= -Wall -Wextra -pedantic -Werror -g -march=native
ifneq ($(wildcard /usr/local/include/valgrind/valgrind.h /usr/include/valgrind/valgrind.h),)
CFLAGS_DEBUG:= $(CFLAGS_DEBUG) -DUSE_VALGRIND
endif
CFLAGS_RELEASE:= -O3 -march=native -DNDEBUG

CFLAGS?= $(CFLAGS_DEBUG)

PAGESIZE?=$(shell getconf PAGESIZE)

ifeq ($(EXECINFO),)
ifneq ($(wildcard /usr/include/execinfo.h),)
EXECINFO:=1
else
EXECINFO:=0
endif
endif

CFLAGS:= \
	-DSTRAND_PAGESIZE=$(PAGESIZE) \
	-DSTRAND_EXECINFO=$(EXECINFO) \
	$(CFLAGS) -std=gnu99 -fno-omit-frame-pointer -MMD -MP
ifeq ($(EXECINFO),1)
ifneq ($(wildcard /usr/lib/libexecinfo.so),)
LDFLAGS:= $(LDFLAGS) -lexecinfo
endif
endif

SRC:= src/strand.c
TEST:= test/strand.c
OBJ:= $(SRC:src/%.c=build/obj/%.o)

test: $(TEST:test/%.c=build/bin/test-%)
	@for t in $^; do ./$$t; done

build/bin/%: build/obj/%.o $(OBJ) | build/bin
	$(CC) $(LDFLAGS) $^ -o $@

build/obj/%.o: src/%.c Makefile | build/obj
	$(CC) $(CFLAGS) -c $< -o $@

build/obj/test-%.o: test/%.c Makefile | build/obj
	$(CC) $(CFLAGS) -c $< -o $@

build/obj build/bin:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all test clean
.PRECIOUS: build/obj/%.o

-include $(OBJ:.o=.o.d)

