#!/usr/bin/make -f
PROJECT_NAME := arena
CC := gcc

SRCS_DIR := .
SRCS = $(SRCS_DIR)/arena.c
OBJS_DIR := obj
OBJS = $(SRCS:$(SRCS_DIR)/%.c=$(OBJS_DIR)/%.o)

TESTS_DIR := tests
TESTS = $(TESTS_DIR)/test_arena.c
TESTS_OBJ_DIR := $(TESTS_DIR)/obj
TESTS_BIN_DIR := $(TESTS_DIR)/bin
TESTS_OBJS = $(TESTS:$(TESTS_DIR)/%.c=$(TESTS_OBJ_DIR)/%.o)
TESTS_BINS = $(TESTS:$(TESTS_DIR)/%.c=$(TESTS_BIN_DIR)/%)

DEPENDENCIES = $(OBJS:.o=.d) $(TESTS_OBJS:.o=.d)

# directories with header files
INCLUDE_DIRS := $(SRCS_DIR)

SHARED_LIB := lib$(PROJECT_NAME).so
STATIC_LIB := lib$(PROJECT_NAME).a

# https://clang.llvm.org/docs/AddressSanitizer.html
SANITISER_ADDR := -fsanitize=address
# https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
SANITISER_UNDEF := -fsanitize=undefined
# https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html
# https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html#index-MMD
C_STANDARD := --std=c17
CPPFLAGS := -MMD
DEBUG_FLAGS := -g -fno-omit-frame-pointer
INCLUDE_FLAGS = $(addprefix -I,$(INCLUDE_DIRS))
OPTIMISATION := -Og
WARN_FLAGS := -pedantic -Wall -Wextra -Werror

SANITISERS = $(SANITISER_ADDR) $(SANITISER_UNDEF)
CFLAGS = $(C_STANDARD) $(WARN_FLAGS) $(INCLUDE_FLAGS) $(CPPFLAGS) $(OPTIMISATION) $(DEBUG_FLAGS) $(SANITISERS)

ARFLAGS := -rvcsD

.PHONY: install
install: $(SHARED_LIB)
	sudo cp $(PROJECT_NAME).h /usr/local/include
	sudo mv $< /usr/local/lib
	sudo ldconfig

$(SHARED_LIB): CFLAGS += -fpic
$(SHARED_LIB): CPPFLAGS += -DNDEBUG
$(SHARED_LIB): DEBUG_FLAGS :=
$(SHARED_LIB): OPTIMISATION := -O3
$(SHARED_LIB): SANITISER_ADDR :=
$(SHARED_LIB): SANITISER_UNDEF :=
$(SHARED_LIB): $(OBJS)
	$(CC) $(CFLAGS) -shared -o $@ $^

$(STATIC_LIB): CPPFLAGS += -DNDEBUG
$(STATIC_LIB): DEBUG_FLAGS :=
$(SHARED_LIB): OPTIMISATION := -O3
$(STATIC_LIB): SANITISER_ADDR :=
$(STATIC_LIB): SANITISER_UNDEF :=
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

$(TESTS_BIN_DIR)/test_%: INCLUDE_DIRS += tau
$(TESTS_BIN_DIR)/test_%: $(TESTS_OBJ_DIR)/test_%.o $(OBJS) | $(TESTS_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	$(RM) -vrd --preserve-root -- $(OBJS_DIR) $(TESTS_OBJ_DIR) $(TESTS_BIN_DIR) $(SHARED_LIB) $(STATIC_LIB)

sinclude $(DEPENDENCIES)
