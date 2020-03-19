.PHONY: test cloc debug write_build_type clean clean_nat

SRC := src
LIB := lib/natalie
OBJ := obj
ONIGMO := ext/onigmo
BDWGC := ext/bdwgc/include

# debug, coverage, or release
BUILD := debug

cflags.debug := -g -Wall -Wextra -Werror -Wno-unused-parameter
cflags.coverage := ${cflags.debug} -fprofile-arcs -ftest-coverage
cflags.release := -O3
CFLAGS := ${cflags.${BUILD}} -pthread

ifdef NAT_WITHOUT_GC
  CFLAGS += -DNAT_WITHOUT_GC
endif

HAS_TTY := $(shell test -t 1 && echo yes || echo no)
ifeq ($(HAS_TTY),yes)
  DOCKER_FLAGS := -i -t
endif

SOURCES := $(filter-out $(SRC)/main.c, $(wildcard $(SRC)/*.c))
OBJECTS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SOURCES))

NAT_SOURCES := $(wildcard $(SRC)/*.nat)
NAT_OBJECTS := $(patsubst $(SRC)/%.nat, $(OBJ)/nat/%.o, $(NAT_SOURCES))

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))

build: write_build_type ext/onigmo/.libs/libonigmo.a ext/bdwgc/.libs/libgc.la $(OBJECTS) $(NAT_OBJECTS)

write_build_type:
	@echo $(BUILD) > .build

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -I$(ONIGMO) -I$(BDWGC) -fPIC -c $< -o $@

$(OBJ)/nat/%.o: $(SRC)/%.nat
	bin/natalie --compile-obj $@ $<

ext/onigmo/.libs/libonigmo.a:
	cd ext/onigmo && ./autogen.sh && ./configure --with-pic && make

ext/bdwgc/.libs/libgc.la:
	cd ext/bdwgc && ./autogen.sh && ./configure --enable-static --with-pic --enable-threads=posix --enable-thread-local-alloc --enable-parallel-mark && make

clean: clean_nat clean_onigmo clean_bdwgc

clean_nat:
	rm -f $(OBJ)/*.o $(OBJ)/nat/*.o

clean_onigmo:
	cd ext/onigmo && make clean || true

clean_bdwgc:
	cd ext/bdwgc && make clean || true

test: build
	ruby test/all.rb

test_valgrind: build
	bin/natalie -c assign_test test/natalie/assign_test.nat
	valgrind --leak-check=no --track-origins=yes --error-exitcode=1 ./assign_test

coverage_report:
	lcov -c --directory . --output-file coverage.info
	genhtml coverage.info --output-directory coverage-report

docker_build:
	docker build -t natalie .

docker_test: docker_build
	docker run $(DOCKER_FLAGS) --rm --entrypoint make natalie test

docker_test_clang: docker_build
	docker run $(DOCKER_FLAGS) -e "CC=/usr/bin/clang" --rm --entrypoint make natalie clean test

docker_test_valgrind: docker_build
	docker run $(DOCKER_FLAGS) --rm --entrypoint make -e NAT_WITHOUT_GC=true natalie clean_nat test_valgrind

docker_coverage_report: docker_build
	rm -rf coverage-report
	mkdir coverage-report
	docker run $(DOCKER_FLAGS) -v $(CURDIR)/coverage-report:/natalie/coverage-report --rm --entrypoint bash natalie -c "make BUILD=coverage clean_nat test; make coverage_report"

cloc:
	cloc --not-match-f=hashmap.\* --not-match-f=compile_commands.json --exclude-dir=.cquery_cache,.github,ext .
