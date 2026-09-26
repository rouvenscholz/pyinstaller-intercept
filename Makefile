

LIB=pyinstaller-inject.so

SRC_DIR=src
BUILD_DIR=build

LIB_SRCS=$(SRC_DIR)/pyinstaller-inject.c $(SRC_DIR)/moduleedit.py

DLSYM_INTERCEPT_DIR=dlsym-intercept
DLSYM_INTERCEPT_LIB=$(DLSYM_INTERCEPT_DIR)/dlysym-intercept.o
DLSYM_INTERCEPT_INCLUDE=$(DLSYM_INTERCEPT_DIR)/include

EXAMPLE_PROGS=example1 example2
EXAMPLE_INJECTS=$(patsubst example%,example-inject%.py,$(EXAMPLE_PROGS))
EXAMPLE_SRC_DIR=examples
EXAMPLE_BUILD_DIR=$(BUILD_DIR)/$(EXAMPLE_SRC_DIR)
VENV=$(EXAMPLE_BUILD_DIR)/venv
PYINSTALLER=$(VENV)/bin/pyinstaller


DEBUG ?= 0
MAXERRORS ?= 1
PYTHON_INCLUDE := $(shell python3 -c 'from sysconfig import get_paths; print(get_paths()["include"])')
INCLUDES=-I./include -I./$(DLSYM_INTERCEPT_INCLUDE) -I$(PYTHON_INCLUDE)


CFLAGS=-Wall -Wextra -Werror -std=gnu11 -fpic
LFLAGS=
LDLIBS=-lelf



LIB_OBJS = $(patsubst src/%, $(BUILD_DIR)/%, $(addsuffix .o,$(basename $(filter %.c, $(LIB_SRCS)))))
LIB_OBJS += $(patsubst src/%, $(BUILD_DIR)/%, $(addsuffix .xxd.o,$(basename $(filter %.py, $(LIB_SRCS)))))

# detect used compiler
CC_COMPILER=$(notdir $(shell readlink -f $(shell which $(CC))))


ifneq (,$(findstring gcc, $(CC_COMPILER)))
CFLAGS+=-fmax-errors=$(MAXERRORS)
else ifneq (,$(findstring clang, $(CC_COMPILER)))
CFLAGS+=-ferror-limit=$(MAXERRORS)
endif



ifeq ($(DEBUG), 0)
	CFLAGS+=-O3 -fvisibility=hidden
	LFLAGS+=-s
else
	CFLAGS+=-DDEBUG -g -O0
endif


.PHONY: all clean example

all: $(LIB)

examples: $(EXAMPLE_PROGS) $(EXAMPLE_INJECTS) $(LIB)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

$(BUILD_DIR)/%.xxd.c: $(SRC_DIR)/%.py | $(BUILD_DIR)/
	sh -c "cat $^; head -c 1 /dev/zero" | xxd -i -n $(notdir $(basename $^)) | sed 's/^unsigned/const unsigned/g' > $@

$(BUILD_DIR)/%.xxd.o: $(BUILD_DIR)/%.xxd.c | $(BUILD_DIR)/
	$(CC) $(CFLAGS) -o $@ -c $<

$(LIB): $(DLSYM_INTERCEPT_LIB) $(LIB_OBJS)
	$(CXX) -shared $(LFLAGS) -o $@ $^ $(LDLIBS)

$(DLSYM_INTERCEPT_LIB):
	$(MAKE) -C $(DLSYM_INTERCEPT_DIR)

$(VENV):
	python3 -m venv $(VENV)

$(PYINSTALLER): $(VENV)
	$(VENV)/bin/pip install pyinstaller requests

example-inject%.py: $(EXAMPLE_SRC_DIR)/example-inject%.py
	cp $^ $@

example%: $(EXAMPLE_SRC_DIR)/example%.py | $(PYINSTALLER)
	LANG= $(PYINSTALLER) --specpath $(EXAMPLE_BUILD_DIR) --workpath $(EXAMPLE_BUILD_DIR) --log-level WARN --strip --onefile --distpath . --name $@ $^

%/:
	mkdir -p $@

clean:
	rm -fr $(BUILD_DIR) $(LIB) $(EXAMPLE_PROGS) $(EXAMPLE_INJECTS)
	$(MAKE) -C $(DLSYM_INTERCEPT_DIR) clean
