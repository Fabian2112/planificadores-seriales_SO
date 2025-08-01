#ifndef CACHE_H_
#define CACHE_H_

#include "cpu.h"
#include <conexion.h>  // Para entrada_cache_t

#define TAMANIO_PAGINA 4096  // 4KB por página

// Funciones principales de la caché
void inicializar_cache(int entradas, char* algoritmo);
void limpiar_cache();
void destruir_cache();

// Operaciones de acceso a la caché
int buscar_pagina_en_cache(int pagina, int pid);
void* obtener_contenido_cache(int pagina, int pid);
bool cargar_pagina_en_cache(int pagina, int marco, int pid, void* contenido, int conexion_memoria);
void marcar_pagina_modificada(int pagina, int pid);

// Operaciones de escritura en caché
bool escribir_en_cache(int pagina, int pid, int offset, void* datos, size_t tamanio);
bool leer_de_cache(int pagina, int pid, int offset, void* buffer, size_t tamanio);

// Gestión de procesos
void desalojar_proceso_cache(int pid, int conexion_memoria);
void sincronizar_paginas_modificadas(int pid, int conexion_memoria);

// Funciones auxiliares
void imprimir_cache();
bool cache_habilitada();

#endif /* CACHE_H_ */
