# Do not use make's built-in rules and variables
MAKEFLAGS += -rR

# Delete targets of failed rules by default
.DELETE_ON_ERROR:

Q := @
ifneq ($(filter-out 0,$(V)),)
  Q :=
endif

srcdir := src
objdir := obj
toolsdir := tools

CC := clang
CFLAGS := -Wall --std=gnu11

ifneq ($(filter-out 0,$(CONFIG_DEBUG)),)
  CFLAGS += -Og -ggdb3 -DEP_DEBUG
else
  CFLAGS += -O2
endif

ifneq ($(filter-out 0,$(CONFIG_TEST)),)
  LDFLAGS += -Wl,-Ttest.lds
  CFLAGS += -DEP_CONFIG_TEST
  EXTRA_DEPS += test.lds
endif

TESTGEN := $(srcdir)/6502_tests.inc

all:

#
# Target gathering
#

objs :=

# Find all rules.mk files to include as list of subdirs
find-subdirs = $(patsubst %/rules.mk,%,$(shell find $(srcdir) -mindepth 1 -name rules.mk))

# expand per-subdir include body
define include-subdir
  srcdir-m := $1
  objdir-m := $(subst $(srcdir),$(objdir),$1)
  include $$(srcdir-m)/rules.mk
  objs += $$(patsubst %.o,$$(objdir-m)/%.o,$$(objs-m))
endef

# Walk the subfolders and include rules.mk from each
$(foreach subdir,$(sort $(call find-subdirs)),\
	$(eval $(call include-subdir,$(subdir))))

.PHONY: all
all: $(objdir)/epoch

$(objdir)/epoch: $(objs) $(EXTRA_DEPS)
	$(Q) $(CC) $(LDFLAGS) -o $@ $(objs)

.PHONY: clean
clean:
	$(Q) rm -rf $(objdir) $(TESTGEN)

.PHONY: show-vars
show-vars:
	# $(call find-subdirs)
	# $(objs)

$(objdir)/%.o: $(srcdir)/%.c
	$(Q) $(CC) $(CFLAGS) -c -o $@ $<

$(objdir)/%.o: $(srcdir)/%.cpp
	$(Q) $(CXX) $(CXXFLAGS) -c -o $@ $<

ifneq ($(filter-out 0,$(CONFIG_TEST)),)
$(srcdir)/2a0x.c: $(srcdir)/6502_tests.inc
endif

$(srcdir)/6502_tests.inc: $(toolsdir)/gen_6502_tests.py
	$(Q) python3 $^ > $@

#
# Dependency generation
#

ifneq ($(filter-out clean show-vars,$(or $(MAKECMDGOALS),all)),)
  $(if $(objs),$(shell mkdir -p $(dir $(objs))))
  include $(subst .o,.d,$(objs))
endif

# Accepts a compiler command line to generate a .d file and patches it with correct object paths
# We want to transform compiler-generated "src.o: ..." into "$(objdir)/src.o $(objdir)/src.d: ..."
define gen-dep-target
  $1 > $@.$$$$ && sed 's,$(*F).o\s*:,$(objdir)/$*.o $@:,g' < $@.$$$$ > $@; rm -f $@.$$$$
endef

$(objdir)/%.d: $(srcdir)/%.c
	$(Q) $(call gen-dep-target,$(CC) -M $(CFLAGS) $<)

$(objdir)/%.d: $(srcdir)/%.cpp
	$(Q) $(call gen-dep-target,$(CXX) -M $(CXXFLAGS) $<)
