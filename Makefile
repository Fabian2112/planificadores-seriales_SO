# Makefile raíz en ./tp/

CC = gcc
CFLAGS = -Wall -g -Iutils -D_POSIX_C_SOURCE=200112L
LDFLAGS = -lcommons -lpthread

BIN_DIR = bin

# Archivos fuente (automático, pero limitado a cada módulo)
MEMORIA_SRC = $(wildcard memoria/*.c) utils/utils.c
KERNEL_SRC  = $(wildcard kernel/*.c)  utils/utils.c
CPU_SRC     = $(wildcard cpu/*.c)     utils/utils.c
IO_SRC      = $(wildcard io/*.c)      utils/utils.c

# Binarios generados
MEMORIA_BIN = $(BIN_DIR)/memoria
KERNEL_BIN  = $(BIN_DIR)/kernel
CPU_BIN     = $(BIN_DIR)/cpu
IO_BIN      = $(BIN_DIR)/io

.PHONY: all clean clean-all memoria kernel cpu io

all: $(MEMORIA_BIN) $(KERNEL_BIN) $(CPU_BIN) $(IO_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(MEMORIA_BIN): $(MEMORIA_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(KERNEL_BIN): $(KERNEL_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CPU_BIN): $(CPU_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(IO_BIN): $(IO_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

memoria: $(MEMORIA_BIN)
kernel:  $(KERNEL_BIN)
cpu:     $(CPU_BIN)
io:      $(IO_BIN)

clean:
	rm -rf $(BIN_DIR)
	@echo "🧹 Binarios eliminados"

clean-all: clean
	find . -name "*.log" -type f -delete
	@echo "🧹 Logs eliminados también"
