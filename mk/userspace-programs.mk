# -----------------------------------------------------------------------------
# Bootstrap userspace programs
# -----------------------------------------------------------------------------

USER_PROGRAM_BUILD_DIR := $(BUILD_DIR)/user-programs

USER_PROGRAM_NAMES := true false echo pwd cat uname sh

USER_PROGRAM_OBJECTS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .o,$(filter-out sh,$(USER_PROGRAM_NAMES))))

.SECONDARY: $(USER_PROGRAM_OBJECTS)

USER_PROGRAM_ELFS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAM_NAMES)))

SH_SOURCE_DIR  := user/bootstrap/bin/sh/src
SH_INCLUDE_DIR := user/bootstrap/bin/sh/include
SH_TEST_DIR    := user/bootstrap/bin/sh/tests

SH_MAIN_OBJECT   := $(USER_PROGRAM_BUILD_DIR)/sh.o
SH_PARSER_OBJECT := $(USER_PROGRAM_BUILD_DIR)/sh-parser.o
SH_EXECUTE_OBJECT := $(USER_PROGRAM_BUILD_DIR)/sh-execute.o
SH_BUILTIN_OBJECT := $(USER_PROGRAM_BUILD_DIR)/sh-builtin.o
SH_OBJECTS := \
	$(SH_MAIN_OBJECT) \
	$(SH_PARSER_OBJECT) \
	$(SH_EXECUTE_OBJECT) \
	$(SH_BUILTIN_OBJECT)

.SECONDARY: $(SH_OBJECTS)

$(SH_MAIN_OBJECT): $(SH_SOURCE_DIR)/main.c \
	$(SH_INCLUDE_DIR)/sh/parser.h \
	$(SH_INCLUDE_DIR)/sh/execute.h \
	$(SH_INCLUDE_DIR)/sh/builtin.h \
	| $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-I$(SH_INCLUDE_DIR) \
		-c $< \
		-o $@

$(SH_PARSER_OBJECT): \
	$(SH_SOURCE_DIR)/parser.c \
	$(SH_INCLUDE_DIR)/sh/parser.h \
	| $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-I$(SH_INCLUDE_DIR) \
		-c $< \
		-o $@
$(SH_EXECUTE_OBJECT): \
	$(SH_SOURCE_DIR)/execute.c \
	$(SH_INCLUDE_DIR)/sh/execute.h \
	| $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-I$(SH_INCLUDE_DIR) \
		-c $< \
		-o $@
$(SH_BUILTIN_OBJECT): \
	$(SH_SOURCE_DIR)/builtin.c \
	$(SH_INCLUDE_DIR)/sh/builtin.h \
	| $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-I$(SH_INCLUDE_DIR) \
		-c $< \
		-o $@

$(USER_PROGRAM_BUILD_DIR)/sh.elf: \
	$(SH_OBJECTS) \
	$(USER_BOOTSTRAP_RUNTIME_OBJECTS) \
	$(USER_BOOTSTRAP_LINKER_SCRIPT)
	$(LD_LLD) \
		-static \
		--build-id=none \
		-T $(USER_BOOTSTRAP_LINKER_SCRIPT) \
		-o $@ \
		$(USER_BOOTSTRAP_RUNTIME_OBJECTS) \
		$(SH_OBJECTS)

$(USER_PROGRAM_BUILD_DIR):
	mkdir -p $@

$(USER_PROGRAM_BUILD_DIR)/%.o: user/bootstrap/bin/%/main.c | $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-c $< \
		-o $@

$(USER_PROGRAM_BUILD_DIR)/%.elf: \
	$(USER_PROGRAM_BUILD_DIR)/%.o \
	$(USER_BOOTSTRAP_RUNTIME_OBJECTS) \
	$(USER_BOOTSTRAP_LINKER_SCRIPT)
	$(LD_LLD) \
		-static \
		--build-id=none \
		-T $(USER_BOOTSTRAP_LINKER_SCRIPT) \
		-o $@ \
		$(USER_BOOTSTRAP_RUNTIME_OBJECTS) \
		$(filter-out $(USER_BOOTSTRAP_RUNTIME_OBJECTS),$(filter %.o,$^))

.PHONY: test-sh-parser

test-sh-parser:
	mkdir -p $(BUILD_DIR)/host-tests
	cc -std=c11 \
		-Wall -Wextra -Werror -Wpedantic \
		-I$(SH_INCLUDE_DIR) \
		$(SH_SOURCE_DIR)/parser.c \
		$(SH_TEST_DIR)/parser_test.c \
		-o $(BUILD_DIR)/host-tests/sh-parser-test
	$(BUILD_DIR)/host-tests/sh-parser-test

.PHONY: test-sh-builtins

test-sh-builtins:
	mkdir -p $(BUILD_DIR)/host-tests
	cc -std=c11 \
		-Wall -Wextra -Werror -Wpedantic \
		-I$(MYOS_INCLUDE_DIR) \
		-I$(USER_RUNTIME_INCLUDE_DIR) \
		-I$(SH_INCLUDE_DIR) \
		$(SH_SOURCE_DIR)/builtin.c \
		$(SH_TEST_DIR)/builtin_test.c \
		-o $(BUILD_DIR)/host-tests/sh-builtin-test
	$(BUILD_DIR)/host-tests/sh-builtin-test
