include settings.mk

CC = gcc
AR = ar
ARFLAGS = rcs

# Directorios
UTILS_DIR = ../utils/src
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Archivos
SRCS_C = $(shell find $(SRC_DIR) -name '*.c')
SRCS_H = $(shell find $(SRC_DIR) -name '*.h')
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS_C))
BIN = $(BIN_DIR)/kernel  # Cambiado a binario ejecutable (no .a)

# Flags
CFLAGS = -g -Wall -Wextra -I../utils/include -I../utils/src -Iinclude



LDFLAGS = -L../utils/bin -lutils -lcommons -lreadline -lncurses


.PHONY: all
all: debug

.PHONY: debug
debug: $(BIN)

.PHONY: release
release: CFLAGS += -O2
release: $(BIN)

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Dependencia explícita para libutils.a
../utils/bin/libutils.a:
	$(MAKE) -C ../utils all

# Regla principal
$(BIN): $(OBJS) ../utils/bin/libutils.a | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Regla para objetos
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRCS_H) | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Crear directorios
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@