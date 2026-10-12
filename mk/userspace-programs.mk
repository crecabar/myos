# -----------------------------------------------------------------------------
# Bootstrap userspace programs
# -----------------------------------------------------------------------------

USER_PROGRAM_BUILD_DIR := $(BUILD_DIR)/user-programs

USER_PROGRAM_NAMES := true false echo pwd cat uname sh

USER_PROGRAM_OBJECTS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .o,$(USER_PROGRAM_NAMES)))

.SECONDARY: $(USER_PROGRAM_OBJECTS)

USER_PROGRAM_ELFS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAM_NAMES)))

SH_PARSER_OBJECT := $(USER_PROGRAM_BUILD_DIR)/sh-parser.o

$(SH_PARSER_OBJECT): \
	user/bootstrap/bin/sh/parser.c \
	user/bootstrap/bin/sh/parser.h \
	| $(USER_PROGRAM_BUILD_DIR)
	$(CLANG) $(USER_BOOTSTRAP_CFLAGS) \
		-c $< \
		-o $@

$(USER_PROGRAM_BUILD_DIR)/sh.elf: $(SH_PARSER_OBJECT)

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
		user/bootstrap/bin/sh/parser.c \
		user/tests/sh_parser_test.c \
		-o $(BUILD_DIR)/host-tests/sh-parser-test
	$(BUILD_DIR)/host-tests/sh-parser-test
