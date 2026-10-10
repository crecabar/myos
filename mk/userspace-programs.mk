# -----------------------------------------------------------------------------
# Bootstrap userspace programs
# -----------------------------------------------------------------------------

USER_PROGRAM_BUILD_DIR := $(BUILD_DIR)/user-programs

USER_PROGRAM_NAMES := true false echo pwd cat

USER_PROGRAM_OBJECTS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .o,$(USER_PROGRAM_NAMES)))

.SECONDARY: $(USER_PROGRAM_OBJECTS)

USER_PROGRAM_ELFS := $(addprefix $(USER_PROGRAM_BUILD_DIR)/,$(addsuffix .elf,$(USER_PROGRAM_NAMES)))

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
		$<
