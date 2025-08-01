#include "mmu.h"
#include "cpu.h"
#include "tlb.h"
#include "cache.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include <commons/config.h>
#include <commons/log.h>
#include <conexiones.h>

// Prototipos de funciones internas
static void* solicitar_direccion_fisica_multinivel(int nro_pagina, int pid, int conexion_memoria);
static void* cargar_pagina_desde_memoria(int nro_pagina, int pid, int conexion_memoria);
static int calcular_entrada_nivel(int nro_pagina, int nivel, int cant_entradas_tabla, int niveles);


void iniciar_MMU() {
    int entradas_tlb = config_get_int_value(cpu_config, "ENTRADAS_TLB");
    char* algoritmo_tlb = config_get_string_value(cpu_config, "REEMPLAZO_TLB");
    
    int entradas_cache = config_get_int_value(cpu_config, "ENTRADAS_CACHE");
    char* algoritmo_cache = config_get_string_value(cpu_config, "REEMPLAZO_CACHE");
    
    iniciar_tlb(entradas_tlb, algoritmo_tlb, cpu_logger);
    inicializar_cache(entradas_cache, algoritmo_cache);
    
    log_info(cpu_logger, "MMU iniciada - TLB: %d entradas (%s), Caché: %d entradas (%s)", entradas_tlb, algoritmo_tlb, entradas_cache, algoritmo_cache);
}

int* obtener_direccion_fisica(int dir, int pid, int conexion_memoria) {
    int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
    
    // Calcular número de página y desplazamiento según las fórmulas especificadas
    int nro_pagina = floor(dir / tamanio_pagina);
    int desplazamiento = dir % tamanio_pagina;

    if (desplazamiento >= tamanio_pagina) {
        log_error(cpu_logger, "ERROR: desplazamiento %d >= tamaño_página %d", desplazamiento, tamanio_pagina);
        return NULL;
    }

    void* frame = NULL;
    int entradas_tlb = config_get_int_value(cpu_config, "ENTRADAS_TLB");
    
    // Consultar TLB si está habilitada
    if (entradas_tlb != 0) {
        frame = consultar_tlb(pid, nro_pagina);
    }

    // TLB HIT - usar el frame de la TLB
    if (frame) {
        log_info(cpu_logger, "PID: %d - OBTENER MARCO - Página: %d - Marco: %p", pid, nro_pagina, frame);
        return (int*)(frame + desplazamiento);
    } 
    // TLB MISS - solicitar traducción a memoria
    else {
        frame = solicitar_direccion_fisica_multinivel(nro_pagina, pid, conexion_memoria);
        if (!frame) {
            log_error(cpu_logger, "Error al obtener marco físico para página %d del proceso %d", nro_pagina, pid);
            return NULL;
        }
        
        // Actualizar TLB con la nueva traducción
        if (entradas_tlb != 0) {
            actualizar_tlb(pid, nro_pagina, frame);
        }
        
        log_info(cpu_logger, "PID: %d - OBTENER MARCO - Página: %d - Marco: %p", pid, nro_pagina, frame);
        return (int*)(frame + desplazamiento);
    }
}

// Nueva función que integra la caché de páginas
int* obtener_direccion_fisica_con_cache(int dir, int pid, int conexion_memoria) {
    int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
    
    // Calcular número de página y desplazamiento
    int nro_pagina = floor(dir / tamanio_pagina);
    int desplazamiento = dir % tamanio_pagina;

    if (desplazamiento >= tamanio_pagina) {
        log_error(cpu_logger, "ERROR: desplazamiento %d >= tamaño_página %d", desplazamiento, tamanio_pagina);
        return NULL;
    }

    log_trace(cpu_logger, "PID: %d - Traduciendo dirección %d -> Página: %d, Offset: %d", 
              pid, dir, nro_pagina, desplazamiento);

    // 1. VERIFICAR CACHÉ DE PÁGINAS (si está habilitada)
    if (cache_habilitada()) {
        int marco_cache = buscar_pagina_en_cache(nro_pagina, pid);
        if (marco_cache != -1) {
            log_info(cpu_logger, "PID: %d - CACHE HIT - Página: %d -> Marco: %d", pid, nro_pagina, marco_cache);
            return (int*)(intptr_t)(marco_cache + desplazamiento);
        }
    }

    // 2. VERIFICAR TLB (si está habilitada)
    void* frame = NULL;
    int entradas_tlb = config_get_int_value(cpu_config, "ENTRADAS_TLB");
    
    if (entradas_tlb != 0) {
        frame = consultar_tlb(pid, nro_pagina);
        if (frame) {
            log_info(cpu_logger, "PID: %d - TLB HIT - Página: %d -> Marco: %p", pid, nro_pagina, frame);
            
            // Si tenemos caché habilitada, cargar la página
            if (cache_habilitada()) {
                void* contenido_pagina = cargar_pagina_desde_memoria(nro_pagina, pid, conexion_memoria);
                if (contenido_pagina) {
                    cargar_pagina_en_cache(nro_pagina, (intptr_t)frame, pid, contenido_pagina, conexion_memoria);
                    free(contenido_pagina);
                }
            }
            
            return (int*)(frame + desplazamiento);
        }
    }

    // 3. CONSULTAR TABLA DE PÁGINAS EN MEMORIA (última instancia)
    frame = solicitar_direccion_fisica_multinivel(nro_pagina, pid, conexion_memoria);
    if (!frame) {
        log_error(cpu_logger, "Error al obtener marco físico para página %d del proceso %d", nro_pagina, pid);
        return NULL;
    }

    // Actualizar TLB con la nueva traducción
    if (entradas_tlb != 0) {
        actualizar_tlb(pid, nro_pagina, frame);
    }

    // Si tenemos caché habilitada, cargar la página
    if (cache_habilitada()) {
        void* contenido_pagina = cargar_pagina_desde_memoria(nro_pagina, pid, conexion_memoria);
        if (contenido_pagina) {
            cargar_pagina_en_cache(nro_pagina, (intptr_t)frame, pid, contenido_pagina, conexion_memoria);
            free(contenido_pagina);
        }
    }

    log_info(cpu_logger, "PID: %d - MEMORIA - Página: %d -> Marco: %p", pid, nro_pagina, frame);
    return (int*)(frame + desplazamiento);
}

static int calcular_entrada_nivel(int nro_pagina, int nivel, int cant_entradas_tabla, int niveles) {
    // Validar parámetros
    if (nivel < 1 || nivel > niveles || cant_entradas_tabla <= 0 || niveles <= 0) {
        log_error(cpu_logger, "Parámetros inválidos en calcular_entrada_nivel: nivel=%d, niveles=%d, cant_entradas=%d", 
                 nivel, niveles, cant_entradas_tabla);
        return 0;
    }
    
    // Aplicar la fórmula: entrada_nivel_X = floor(nro_página / cant_entradas_tabla^(N-X)) % cant_entradas_tabla
    int exponente = niveles - nivel;
    int divisor = (int)pow(cant_entradas_tabla, exponente);
    
    if (divisor <= 0) {
        log_error(cpu_logger, "Error en cálculo de divisor: cant_entradas_tabla=%d, exponente=%d", 
                 cant_entradas_tabla, exponente);
        return 0;
    }
    
    int entrada = (nro_pagina / divisor) % cant_entradas_tabla;
    
    log_trace(cpu_logger, "Nivel %d: página=%d, divisor=%d, entrada=%d", 
             nivel, nro_pagina, divisor, entrada);
    
    return entrada;
}

static void* solicitar_direccion_fisica_multinivel(int nro_pagina, int pid, int conexion_memoria) {
    int niveles = config_get_int_value(cpu_config, "CANTIDAD_NIVELES");
    int cant_entradas_tabla = config_get_int_value(cpu_config, "ENTRADAS_POR_TABLA");

    // Validar configuración
    if (niveles <= 0 || cant_entradas_tabla <= 0) {
        log_error(cpu_logger, "Configuración inválida: niveles=%d, entradas_por_tabla=%d", niveles, cant_entradas_tabla);
        return NULL;
    }

    // Calcular entradas para cada nivel usando las fórmulas especificadas
    int entradas[niveles];
    for (int nivel = 1; nivel <= niveles; nivel++) {
        entradas[nivel - 1] = calcular_entrada_nivel(nro_pagina, nivel, cant_entradas_tabla, niveles);
    }

    // Log de la traducción multinivel
    char entradas_str[256] = {0};
    snprintf(entradas_str, sizeof(entradas_str), "[");
    for (int i = 0; i < niveles; i++) {
        if (i > 0) strcat(entradas_str, ", ");
        char temp[16];
        snprintf(temp, sizeof(temp), "%d", entradas[i]);
        strcat(entradas_str, temp);
    }
    strcat(entradas_str, "]");
    
    log_info(cpu_logger, "PID: %d - Acceso Tabla de Páginas - Página: %d - Entradas: %s", 
             pid, nro_pagina, entradas_str);

    // Comunicación real con memoria para traducción multinivel
    t_paquete* paquete = crear_paquete(SOLICITUD_PAGINA, cpu_logger);
    
    // Código de operación: 1 = Acceso a tabla de páginas
    int operacion = 1;
    agregar_a_paquete(paquete, &operacion, sizeof(int));
    agregar_a_paquete(paquete, &pid, sizeof(int));
    agregar_a_paquete(paquete, &nro_pagina, sizeof(int));
    agregar_a_paquete(paquete, &niveles, sizeof(int));
    agregar_a_paquete(paquete, entradas, niveles * sizeof(int));
    
    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);
    
    // Recibir respuesta de memoria (número de marco)
    int marco_fisico;
    ssize_t bytes_recibidos = recv(conexion_memoria, &marco_fisico, sizeof(int), MSG_WAITALL);
    
    if (bytes_recibidos <= 0) {
        log_error(cpu_logger, "Error al recibir marco físico de memoria para PID: %d, página: %d", pid, nro_pagina);
        return NULL;
    }
    
    if (marco_fisico == -1) {
        log_error(cpu_logger, "Memoria no pudo traducir página %d para PID: %d", nro_pagina, pid);
        return NULL;
    }
    
    // Calcular dirección física del marco
    int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
    void* direccion_marco = (void*)(intptr_t)(marco_fisico * tamanio_pagina);
    
    log_info(cpu_logger, "PID: %d - Marco físico obtenido: %d (dirección: %p) para página %d", pid, marco_fisico, direccion_marco, nro_pagina);
    
    return direccion_marco;
}

static void* cargar_pagina_desde_memoria(int nro_pagina, int pid, int conexion_memoria) {
    log_trace(cpu_logger, "PID: %d - Cargando página %d desde memoria", pid, nro_pagina);
    
    // Crear paquete para solicitar contenido de página
    t_paquete* paquete = crear_paquete(SOLICITUD_PAGINA, cpu_logger);
    
    // Código de operación: 5 = Leer página completa
    int operacion = 5;
    agregar_a_paquete(paquete, &operacion, sizeof(int));
    agregar_a_paquete(paquete, &pid, sizeof(int));
    agregar_a_paquete(paquete, &nro_pagina, sizeof(int));
    
    enviar_paquete(paquete, conexion_memoria);
    eliminar_paquete(paquete);
    
    // Recibir contenido de la página
    int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
    void* contenido = malloc(tamanio_pagina);
    
    ssize_t bytes_recibidos = recv(conexion_memoria, contenido, tamanio_pagina, MSG_WAITALL);
    
    if (bytes_recibidos <= 0) {
        free(contenido);
        log_error(cpu_logger, "Error al cargar página %d del PID %d desde memoria", nro_pagina, pid);
        return NULL;
    }
    
    log_trace(cpu_logger, "PID: %d - Página %d cargada desde memoria (%zd bytes)", pid, nro_pagina, bytes_recibidos);
    return contenido;
}

void limpiar_tlb_proceso(int pid) {
    eliminar_entradas_proceso(pid);
    // También limpiar la caché del proceso
    if (cache_habilitada()) {
        desalojar_proceso_cache(pid, 0); // Sin conexión porque solo limpiamos
    }
}

void finalizar_MMU() {
    destruir_tlb();
    destruir_cache();
    log_info(cpu_logger, "MMU finalizada correctamente");
}