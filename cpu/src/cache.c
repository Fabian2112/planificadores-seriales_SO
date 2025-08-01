#include "cache.h"

// Variable global de la caché
entrada_cache_t* cache = NULL;

// Variables externas de CPU
extern t_log* cpu_logger;
extern t_config* cpu_config;

// Variables externas definidas en utils/src/conexion.c
extern int cantidad_entradas;
extern char* algoritmo_reemplazo;
extern int puntero_clock;

void inicializar_cache(int entradas, char* algoritmo) {
    cantidad_entradas = entradas;
    
    if (entradas <= 0) {
        log_info(cpu_logger, "Caché de páginas deshabilitada (entradas: %d)", entradas);
        cache = NULL;
        return;
    }
    
    algoritmo_reemplazo = strdup(algoritmo);
    cache = calloc(entradas, sizeof(entrada_cache_t));
    
    // Inicializar contenido de cada entrada
    for (int i = 0; i < cantidad_entradas; i++) {
        cache[i].contenido = malloc(TAMANIO_PAGINA);
        cache[i].ocupado = false;
        cache[i].usado = false;
        cache[i].modificado = false;
        cache[i].pid = -1;
        cache[i].pagina = -1;
        cache[i].marco = -1;
    }
    
    puntero_clock = 0;
    log_info(cpu_logger, "Caché de páginas inicializada: %d entradas, algoritmo %s", 
             entradas, algoritmo);
}

void limpiar_cache() {
    if (!cache_habilitada()) return;
    
    for (int i = 0; i < cantidad_entradas; i++) {
        cache[i].ocupado = false;
        cache[i].usado = false;
        cache[i].modificado = false;
        cache[i].pid = -1;
        cache[i].pagina = -1;
        cache[i].marco = -1;
        memset(cache[i].contenido, 0, TAMANIO_PAGINA);
    }
    puntero_clock = 0;
    log_info(cpu_logger, "Caché de páginas limpiada");
}

void destruir_cache() {
    if (cache) {
        for (int i = 0; i < cantidad_entradas; i++) {
            if (cache[i].contenido) {
                free(cache[i].contenido);
            }
        }
        free(cache);
        cache = NULL;
    }
    
    if (algoritmo_reemplazo) {
        free(algoritmo_reemplazo);
        algoritmo_reemplazo = NULL;
    }
    
    log_info(cpu_logger, "Caché de páginas destruida");
}

bool cache_habilitada() {
    return cache != NULL && cantidad_entradas > 0;
}

int buscar_pagina_en_cache(int pagina, int pid) {
    if (!cache_habilitada()) return -1;
    
    // Aplicar retardo de caché
    int retardo_cache = config_get_int_value(cpu_config, "RETARDO_CACHE");
    usleep(retardo_cache * 1000); // Convertir ms a μs
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pagina == pagina && cache[i].pid == pid) {
            // Marcar como usado para algoritmos CLOCK
            cache[i].usado = true;
            
            log_info(cpu_logger, "PID: %d - Cache Hit - Pagina: %d", 
                     pid, pagina);
            return cache[i].marco;
        }
    }
    
    log_info(cpu_logger, "PID: %d - Cache Miss - Pagina: %d", pid, pagina);
    return -1;
}

void* obtener_contenido_cache(int pagina, int pid) {
    if (!cache_habilitada()) return NULL;
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pagina == pagina && cache[i].pid == pid) {
            cache[i].usado = true;  // Marcar como usado
            return cache[i].contenido;
        }
    }
    
    return NULL;
}

// Funciones para escribir página modificada a memoria
static void escribir_pagina_a_memoria(entrada_cache_t* entrada, int conexion_memoria) {
    if (!entrada->modificado) return;
    
    log_info(cpu_logger, "PID: %d - Memory Update - Página: %d - Frame: %d", 
             entrada->pid, entrada->pagina, entrada->marco);
    
    // Crear paquete para escribir página completa
    t_paquete* paquete = crear_paquete(PAQUETE, cpu_logger);
    
    // Código de operación: 4 = Actualizar página completa
    int operacion = 4;
    agregar_a_paquete(paquete, &operacion, sizeof(int));
    agregar_a_paquete(paquete, &(entrada->pid), sizeof(int));
    agregar_a_paquete(paquete, &(entrada->marco), sizeof(int));
    agregar_a_paquete(paquete, entrada->contenido, TAMANIO_PAGINA);
    
    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);
    
    // Esperar confirmación "OK" de memoria
    char respuesta[3];
    ssize_t bytes_recibidos = recv(conexion_memoria, respuesta, 2, MSG_WAITALL);
    
    if (bytes_recibidos > 0) {
        respuesta[2] = '\0';
        if (strcmp(respuesta, "OK") == 0) {
            log_trace(cpu_logger, "Página %d del PID %d sincronizada con memoria", 
                      entrada->pagina, entrada->pid);
        } else {
            log_error(cpu_logger, "Error al sincronizar página con memoria: %s", respuesta);
        }
    } else {
        log_error(cpu_logger, "Error al recibir confirmación de sincronización");
    }
}

// Algoritmo de reemplazo CLOCK
static int seleccionar_victima_clock() {
    int inicio = puntero_clock;
    
    while (true) {
        if (!cache[puntero_clock].usado) {
            int victima = puntero_clock;
            puntero_clock = (puntero_clock + 1) % cantidad_entradas;
            return victima;
        }
        
        cache[puntero_clock].usado = false;
        puntero_clock = (puntero_clock + 1) % cantidad_entradas;
        
        // Evitar bucle infinito
        if (puntero_clock == inicio) {
            break;
        }
    }
    
    // Si todas tenían bit de uso, tomar la actual
    int victima = puntero_clock;
    puntero_clock = (puntero_clock + 1) % cantidad_entradas;
    return victima;
}

// Algoritmo de reemplazo CLOCK-M (Modified)
static int seleccionar_victima_clock_m() {
    int inicio = puntero_clock;
    
    // Primera vuelta: buscar páginas no usadas y no modificadas
    while (true) {
        if (!cache[puntero_clock].usado && !cache[puntero_clock].modificado) {
            int victima = puntero_clock;
            puntero_clock = (puntero_clock + 1) % cantidad_entradas;
            return victima;
        }
        
        puntero_clock = (puntero_clock + 1) % cantidad_entradas;
        if (puntero_clock == inicio) break;
    }
    
    // Segunda vuelta: limpiar bits de uso y buscar no modificadas
    for (int i = 0; i < cantidad_entradas; i++) {
        cache[i].usado = false;
    }
    
    while (true) {
        if (!cache[puntero_clock].modificado) {
            int victima = puntero_clock;
            puntero_clock = (puntero_clock + 1) % cantidad_entradas;
            return victima;
        }
        
        puntero_clock = (puntero_clock + 1) % cantidad_entradas;
        if (puntero_clock == inicio) break;
    }
    
    // Si todas están modificadas, tomar la actual
    int victima = puntero_clock;
    puntero_clock = (puntero_clock + 1) % cantidad_entradas;
    return victima;
}

bool cargar_pagina_en_cache(int pagina, int marco, int pid, void* contenido, int conexion_memoria) {
    if (!cache_habilitada()) return false;
    
    // Buscar entrada libre
    for (int i = 0; i < cantidad_entradas; i++) {
        if (!cache[i].ocupado) {
            cache[i].pagina = pagina;
            cache[i].marco = marco;
            cache[i].pid = pid;
            cache[i].usado = true;
            cache[i].modificado = false;
            cache[i].ocupado = true;
            
            if (contenido) {
                memcpy(cache[i].contenido, contenido, TAMANIO_PAGINA);
            } else {
                memset(cache[i].contenido, 0, TAMANIO_PAGINA);
            }
            
            log_info(cpu_logger, "PID: %d - Cache Add - Pagina: %d", 
                     pid, pagina);
            return true;
        }
    }
    
    // Cache llena, necesario reemplazo
    int victima;
    if (strcmp(algoritmo_reemplazo, "CLOCK-M") == 0) {
        victima = seleccionar_victima_clock_m();
    } else {
        victima = seleccionar_victima_clock();
    }
    
    // Si la víctima está modificada, escribirla a memoria
    if (cache[victima].modificado) {
        log_info(cpu_logger, "Desalojando página modificada - PID: %d, Página: %d", 
                 cache[victima].pid, cache[victima].pagina);
        escribir_pagina_a_memoria(&cache[victima], conexion_memoria);
    }
    
    // Reemplazar entrada
    cache[victima].pagina = pagina;
    cache[victima].marco = marco;
    cache[victima].pid = pid;
    cache[victima].usado = true;
    cache[victima].modificado = false;
    cache[victima].ocupado = true;
    
    if (contenido) {
        memcpy(cache[victima].contenido, contenido, TAMANIO_PAGINA);
    } else {
        memset(cache[victima].contenido, 0, TAMANIO_PAGINA);
    }
    
    log_info(cpu_logger, "PID: %d - Cache Add - Pagina: %d", 
             pid, pagina);
    return true;
}

void marcar_pagina_modificada(int pagina, int pid) {
    if (!cache_habilitada()) return;
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pagina == pagina && cache[i].pid == pid) {
            cache[i].modificado = true;
            cache[i].usado = true;
            log_trace(cpu_logger, "Página marcada como modificada - PID: %d, Página: %d", pid, pagina);
            break;
        }
    }
}

bool escribir_en_cache(int pagina, int pid, int offset, void* datos, size_t tamanio) {
    if (!cache_habilitada()) return false;
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pagina == pagina && cache[i].pid == pid) {
            // Verificar que no se salga de los límites de la página
            if (offset + tamanio > TAMANIO_PAGINA) {
                log_error(cpu_logger, "Escritura fuera de límites - Offset: %d, Tamaño: %zu", offset, tamanio);
                return false;
            }
            
            memcpy((char*)cache[i].contenido + offset, datos, tamanio);
            cache[i].modificado = true;
            cache[i].usado = true;
            
            log_trace(cpu_logger, "Escritura en caché - PID: %d, Página: %d, Offset: %d, Tamaño: %zu", 
                      pid, pagina, offset, tamanio);
            return true;
        }
    }
    
    return false;
}

bool leer_de_cache(int pagina, int pid, int offset, void* buffer, size_t tamanio) {
    if (!cache_habilitada()) return false;
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pagina == pagina && cache[i].pid == pid) {
            // Verificar que no se salga de los límites de la página
            if (offset + tamanio > TAMANIO_PAGINA) {
                log_error(cpu_logger, "Lectura fuera de límites - Offset: %d, Tamaño: %zu", offset, tamanio);
                return false;
            }
            
            memcpy(buffer, (char*)cache[i].contenido + offset, tamanio);
            cache[i].usado = true;
            
            log_trace(cpu_logger, "Lectura de caché - PID: %d, Página: %d, Offset: %d, Tamaño: %zu", 
                      pid, pagina, offset, tamanio);
            return true;
        }
    }
    
    return false;
}

void sincronizar_paginas_modificadas(int pid, int conexion_memoria) {
    if (!cache_habilitada()) return;
    
    log_info(cpu_logger, "Sincronizando páginas modificadas del PID: %d", pid);
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pid == pid && cache[i].modificado) {
            escribir_pagina_a_memoria(&cache[i], conexion_memoria);
            cache[i].modificado = false; // Ya no está modificada
        }
    }
}

void desalojar_proceso_cache(int pid, int conexion_memoria) {
    if (!cache_habilitada()) return;
    
    log_info(cpu_logger, "Desalojando proceso PID: %d de la caché", pid);
    
    // Primero sincronizar páginas modificadas
    sincronizar_paginas_modificadas(pid, conexion_memoria);
    
    // Luego eliminar todas las entradas del proceso
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado && cache[i].pid == pid) {
            log_trace(cpu_logger, "Eliminando página %d del PID %d de la caché (entrada %d)", 
                      cache[i].pagina, pid, i);
            
            cache[i].ocupado = false;
            cache[i].usado = false;
            cache[i].modificado = false;
            cache[i].pid = -1;
            cache[i].pagina = -1;
            cache[i].marco = -1;
            memset(cache[i].contenido, 0, TAMANIO_PAGINA);
        }
    }
    
    log_info(cpu_logger, "Proceso PID: %d desalojado completamente de la caché", pid);
}

void imprimir_cache() {
    if (!cache_habilitada()) {
        log_info(cpu_logger, "Caché de páginas deshabilitada");
        return;
    }
    
    log_info(cpu_logger, "=== Estado actual de la caché de páginas ===");
    log_info(cpu_logger, "Algoritmo: %s, Entradas: %d, Puntero: %d", 
             algoritmo_reemplazo, cantidad_entradas, puntero_clock);
    
    for (int i = 0; i < cantidad_entradas; i++) {
        if (cache[i].ocupado) {
            log_info(cpu_logger, "Entrada %d -> PID: %d, Página: %d, Marco: %d, U: %d, M: %d", 
                     i, cache[i].pid, cache[i].pagina, cache[i].marco, 
                     cache[i].usado, cache[i].modificado);
        } else {
            log_info(cpu_logger, "Entrada %d -> [LIBRE]", i);
        }
    }
    log_info(cpu_logger, "==========================================");
}
