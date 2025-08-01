include settings.mk

CC = gcc
AR = ar
ARFLAGS = rcs

# Encuentra todos los archivos fuente .c
SRCS_C = $(shell find src/ -name '*.c')

# Directorios
OBJ_DIR = obj
BIN_DIR = bin

# Genera paths de objetos (.o) y dependencias (.d)
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS_C))
DEPS = $(OBJS:.o=.d)

# Nombre de la biblioteca estática
BIN = $(BIN_DIR)/libutils.a

# Flags de compilación
CFLAGS = -g -Wall -Wextra -I./include $(DEPFLAGS)
DEPFLAGS = -MMD -MP  # Generación automática de dependencias

.PHONY: all
all: debug

.PHONY: debug
debug: $(BIN)

.PHONY: release
release: CFLAGS += -O2 -DNDEBUG
release: $(BIN)

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Construcción de la biblioteca
$(BIN): $(OBJS) | $(BIN_DIR)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Biblioteca construida: $@"

# Regla para compilar cada .c
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
	@echo "Compilado: $<"

# Incluye archivos de dependencia
-include $(DEPS)

# Creación de directorios
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

# Verificación de contenido (target útil)
.PHONY: check
check: $(BIN)
	@echo "Contenido de la biblioteca:"
	@ar -t $^ | grep -E 'conexion|util'