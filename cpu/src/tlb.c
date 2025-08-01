#include "tlb.h"

// Variables globales para la TLB
int max_entradas;
char* algoritmo;
int contador_orden = 0; // Para FIFO

void iniciar_tlb(int entradas, char* algoritmo_reemplazo, t_log* logger_rec) {
    if (!logger_rec || !algoritmo_reemplazo) {
        return;
    }
    
    // Limpiar estado previo si existe
    destruir_tlb();
    
    cpu_logger = logger_rec;
    max_entradas = entradas;
    
    // Validar algoritmo
    if (strcmp(algoritmo_reemplazo, "FIFO") != 0 && strcmp(algoritmo_reemplazo, "LRU") != 0) {
        log_error(cpu_logger, "Algoritmo de TLB no válido: %s. Debe ser FIFO o LRU", algoritmo_reemplazo);
        return;
    }
    
    algoritmo = strdup(algoritmo_reemplazo);
    if (!algoritmo) {
        log_error(cpu_logger, "Error al asignar memoria para algoritmo TLB");
        return;
    }
    
    // Si las entradas son 0, la TLB está deshabilitada
    if (entradas == 0) {
        tlb = NULL;
        log_info(cpu_logger, "TLB deshabilitada (entradas = 0)");
        return;
    }
    
    // Validar que las entradas no sean excesivas (protección contra configuraciones erróneas)
    if (entradas > 10000) {
        log_warning(cpu_logger, "Número de entradas TLB muy alto: %d. Se recomienda verificar configuración", entradas);
    }
    
    tlb = list_create();
    if (!tlb) {
        log_error(cpu_logger, "Error al crear la lista TLB");
        free(algoritmo);
        algoritmo = NULL;
        return;
    }
    
    contador_orden = 0;
    log_info(cpu_logger, "TLB inicializada con %d entradas, algoritmo: %s", entradas, algoritmo);
}

void* consultar_tlb(int pid, int pagina) {
    // Si la TLB está deshabilitada, devolver miss inmediatamente
    if (max_entradas == 0 || !tlb) {
        return NULL;
    }
    
    // Buscar la entrada en la TLB
    int size = list_size(tlb);
    for (int i = 0; i < size; i++) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada && entrada->pid == pid && entrada->pagina == pagina) {
            // TLB HIT - actualizar timestamp para LRU
            if (algoritmo && strcmp(algoritmo, "LRU") == 0) {
                entrada->timestamp = time(NULL);
            }
            log_info(cpu_logger, "PID: %d - TLB HIT - Pagina: %d", pid, pagina);
            return entrada->marco;
        }
    }
    
    // TLB MISS
    log_info(cpu_logger, "PID: %d - TLB MISS - Pagina: %d", pid, pagina);
    return NULL;
}

static int encontrar_victima_fifo() {
    int size = list_size(tlb);
    if (size == 0) return 0;
    
    int indice_victima = 0;
    int menor_orden = ((EntradaTLB*)list_get(tlb, 0))->orden_insercion;
    
    for (int i = 1; i < size; i++) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada && entrada->orden_insercion < menor_orden) {
            menor_orden = entrada->orden_insercion;
            indice_victima = i;
        }
    }
    return indice_victima;
}

static int encontrar_victima_lru() {
    int size = list_size(tlb);
    if (size == 0) return 0;
    
    int indice_victima = 0;
    time_t menor_timestamp = ((EntradaTLB*)list_get(tlb, 0))->timestamp;
    
    for (int i = 1; i < size; i++) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada && entrada->timestamp < menor_timestamp) {
            menor_timestamp = entrada->timestamp;
            indice_victima = i;
        }
    }
    return indice_victima;
}

void actualizar_tlb(int pid, int pagina, void* marco) {
    // Si la TLB está deshabilitada, no hacer nada
    if (max_entradas == 0 || !tlb || !algoritmo) {
        return;
    }
    
    // Verificar si la entrada ya existe (actualizar en lugar de duplicar)
    int size = list_size(tlb);
    for (int i = 0; i < size; i++) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada && entrada->pid == pid && entrada->pagina == pagina) {
            entrada->marco = marco;
            entrada->timestamp = time(NULL);
            log_info(cpu_logger, "TLB actualizada - PID: %d, Pagina: %d, Marco: %p", pid, pagina, marco);
            return;
        }
    }
    
    // Si la TLB está llena, aplicar algoritmo de reemplazo
    if (size >= max_entradas) {
        int indice_victima;
        
        if (strcmp(algoritmo, "FIFO") == 0) {
            indice_victima = encontrar_victima_fifo();
        } else { // LRU
            indice_victima = encontrar_victima_lru();
        }
        
        EntradaTLB* victima = list_get(tlb, indice_victima);
        if (victima) {
            log_info(cpu_logger, "Entrada victima: %d - PID: %d, Pagina: %d, Marco: %p", 
                    indice_victima, victima->pid, victima->pagina, victima->marco);
        }
        
        list_remove_and_destroy_element(tlb, indice_victima, free);
    }
    
    // Crear nueva entrada
    EntradaTLB* nueva = malloc(sizeof(EntradaTLB));
    if (!nueva) {
        log_error(cpu_logger, "Error al asignar memoria para nueva entrada TLB");
        return;
    }
    
    nueva->pid = pid;
    nueva->pagina = pagina;
    nueva->marco = marco;
    nueva->timestamp = time(NULL);
    nueva->orden_insercion = contador_orden++;
    
    // Protección contra overflow del contador
    if (contador_orden < 0) {
        contador_orden = 0;
        log_warning(cpu_logger, "Reiniciando contador de orden TLB por overflow");
    }
    
    list_add(tlb, nueva);
    log_info(cpu_logger, "Nueva entrada -> PID: %d, Pagina: %d, Marco: %p", pid, pagina, marco);
}

void limpiar_tlb() {
    if (tlb) {
        list_clean_and_destroy_elements(tlb, free);
        contador_orden = 0;
        log_info(cpu_logger, "TLB limpiada completamente");
    }
}

void eliminar_entradas_proceso(int pid) {
    if (!tlb || max_entradas == 0) {
        return;
    }
    
    int entradas_eliminadas = 0;
    
    // Mostrar estado antes de eliminar (solo en modo debug)
    log_debug(cpu_logger, "Eliminando entradas TLB para proceso PID: %d", pid);
    
    // Iterar desde el final hacia el principio para evitar problemas con índices
    for (int i = list_size(tlb) - 1; i >= 0; i--) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada && entrada->pid == pid) {
            log_debug(cpu_logger, "Eliminando entrada TLB: PID %d, Pagina %d, Marco %p", 
                     entrada->pid, entrada->pagina, entrada->marco);
            list_remove_and_destroy_element(tlb, i, free);
            entradas_eliminadas++;
        }
    }
    
    if (entradas_eliminadas > 0) {
        log_info(cpu_logger, "Eliminadas %d entradas de TLB para el proceso PID: %d", 
                entradas_eliminadas, pid);
    } else {
        log_debug(cpu_logger, "No se encontraron entradas TLB para el proceso PID: %d", pid);
    }
}

void destruir_tlb() {
    // Log solo si el cpu_logger está disponible
    if (cpu_logger && (tlb || algoritmo)) {
        log_info(cpu_logger, "Destruyendo TLB");
    }
    
    // Limpiar lista TLB
    if (tlb) {
        list_destroy_and_destroy_elements(tlb, free);
        tlb = NULL;
    }
    
    // Limpiar algoritmo
    if (algoritmo) {
        free(algoritmo);
        algoritmo = NULL;
    }
    
    // Resetear variables globales
    max_entradas = 0;
    contador_orden = 0;
    
    // No limpiar cpu_logger ya que puede ser usado por otros módulos
    // cpu_logger = NULL;
}

void mostrar_estado_tlb() {
    if (!tlb || max_entradas == 0) {
        log_debug(cpu_logger, "TLB deshabilitada o no inicializada");
        return;
    }
    
    int size = list_size(tlb);
    log_debug(cpu_logger, "Estado TLB - Entradas: %d/%d, Algoritmo: %s", size, max_entradas, algoritmo ? algoritmo : "N/A");
    
    // Para TLB grandes, solo mostrar las primeras entradas para evitar spam
    int max_mostrar = (size > 10) ? 10 : size;
    
    for (int i = 0; i < max_mostrar; i++) {
        EntradaTLB* entrada = list_get(tlb, i);
        if (entrada) {
            log_debug(cpu_logger, "  [%d] PID: %d, Pagina: %d, Marco: %p, Timestamp: %ld, Orden: %d", 
                     i, entrada->pid, entrada->pagina, entrada->marco, 
                     entrada->timestamp, entrada->orden_insercion);
        }
    }
    
    if (size > max_mostrar) {
        log_debug(cpu_logger, "  ... y %d entradas más", size - max_mostrar);
    }
}
