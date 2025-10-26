#!/usr/bin/make -f
PROJECT_NAME := Arena
CC := gcc

SRCS_DIR := .
OBJS_DIR := obj
TESTS_DIR := tests
TESTS_OBJ_DIR := $(TESTS_DIR)/obj
TESTS_BIN_DIR := $(TESTS_DIR)/bin
# directories with header files
INCLUDE_DIRS := $(SRCS_DIR)

SHARED_LIB := lib$(PROJECT_NAME).so
STATIC_LIB := lib$(PROJECT_NAME).a

SRCS = $(SRCS_DIR)/arena.c
OBJS = $(SRCS:$(SRCS_DIR)/%.c=$(OBJS_DIR)/%.o)
TESTS = $(wildcard $(TESTS_DIR)/test_*.c)
TESTS_OBJS = $(TESTS:$(TESTS_DIR)/%.c=$(TESTS_OBJ_DIR)/%.o)
TESTS_BINS = $(TESTS:$(TESTS_DIR)/%.c=$(TESTS_BIN_DIR)/%)
DEPENDENCIES = $(OBJS:.o=.d) $(TESTS_OBJS:.o=.d)

# https://clang.llvm.org/docs/AddressSanitizer.html
ADDRESS_SANITISER := -fsanitize=address
# https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
UNDEFINED_SANITISER := -fsanitize=undefined

INCLUDE_FLAGS = $(addprefix -I,$(INCLUDE_DIRS))
# https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
# https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html#index-MMD
CPPFLAGS := -MMD
OPTIMISATION := -O2
DEBUG_FLAGS := -g3 -fno-omit-frame-pointer
C_STANDARD := --std=c17
WARN_FLAGS := -pedantic -Wall -Wextra -Werror

INSTRUMENTATION = $(ADDRESS_SANITISER) $(UNDEFINED_SANITISER)
CFLAGS = $(C_STANDARD) $(WARN_FLAGS) $(INCLUDE_FLAGS) $(CPPFLAGS) $(OPTIMISATION) $(DEBUG_FLAGS) $(INSTRUMENTATION)

ARFLAGS := -rvcsD

.PHONY: install
install: $(SHARED_LIB)
	sudo cp $(PROJECT_NAME).h /usr/local/include
	sudo mv $< /usr/local/lib
	sudo ldconfig

$(SHARED_LIB): ADDRESS_SANITISER :=
$(SHARED_LIB): UNDEFINED_SANITISER :=
$(SHARED_LIB): DEBUG_FLAGS :=
$(SHARED_LIB): CPPFLAGS += -DNDEBUG
$(SHARED_LIB): CFLAGS += -fpic
$(SHARED_LIB): $(OBJS)
	$(CC) $(CFLAGS) -shared -o $@ $^

$(STATIC_LIB): ADDRESS_SANITISER :=
$(STATIC_LIB): UNDEFINED_SANITISER :=
$(STATIC_LIB): DEBUG_FLAGS :=
$(STATIC_LIB): CPPFLAGS += -DNDEBUG
$(STATIC_LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

.PHONY: run-tests
run-tests: $(TESTS_BINS)
	@for test in $^; \
		do ./$$test --failed-output-only ; \
	done

$(OBJS_DIR) $(TESTS_OBJ_DIR) $(TESTS_BIN_DIR):
	@mkdir -p $@

# https://www.gnu.org/software/make/manual/html_node/Prerequisite-Types.html
$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.c | $(OBJS_DIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(TESTS_OBJ_DIR)/%.o: $(TESTS_DIR)/%.c | $(TESTS_OBJ_DIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(TESTS_BIN_DIR)/test_%: OPTIMISATION := -Og
$(TESTS_BIN_DIR)/test_%: INCLUDE_DIRS += tau
$(TESTS_BIN_DIR)/test_%: $(TESTS_OBJ_DIR)/test_%.o $(OBJS) | $(TESTS_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	$(RM) -vrd --preserve-root -- $(OBJS_DIR) $(TESTS_OBJ_DIR) $(TESTS_BIN_DIR) $(SHARED_LIB) $(STATIC_LIB)

sinclude $(DEPENDENCIES)
