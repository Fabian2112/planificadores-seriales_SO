#include "../include/kernel.h"
#include "../include/pcb.h"
#include <unistd.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// Variables externas
extern t_log* kernel_logger;
extern int TIEMPO_SUSPENSION;
extern t_list* cola_susp_ready;
extern t_list* cola_ready;
extern t_list* cola_blocked;
extern t_list* cola_new;
extern t_list* timers_suspension;
extern pthread_mutex_t mutex_cola_susp_ready;
extern pthread_mutex_t mutex_cola_ready;
extern pthread_mutex_t mutex_cola_blocked;
extern pthread_mutex_t mutex_cola_new;
extern pthread_mutex_t mutex_timers_suspension;

// Planificador de Mediano Plazo - Implementación completa
void* planificador_medio_plazo(void* _) {
    (void)_;
    log_info(kernel_logger, "Planificador de Mediano Plazo iniciado");
    
    while (1) {
        // Verificar memoria disponible y procesar colas
        verificar_memoria_tras_suspension();
        
        // Priorizar procesos SUSP_READY sobre NEW
        priorizar_susp_ready_sobre_new();
        
        // Activar procesos suspendidos si hay memoria disponible
        activar_procesos_susp_ready();
        
        log_trace(kernel_logger, "Planificador mediano plazo verificando estado del sistema...");
        
        // Ejecutar cada segundo para verificación continua
        sleep(1);
    }
    return NULL;
}

// ========== SISTEMA DE TIMERS DE SUSPENSIÓN ==========

// Función para iniciar timer de suspensión automática
void iniciar_timer_suspension(int pid) {
    log_trace(kernel_logger, "Iniciando timer de suspensión para proceso PID=%d", pid);
    
    t_timer_suspension* timer = malloc(sizeof(t_timer_suspension));
    timer->pid = pid;
    timer->tiempo_bloqueo = time(NULL);
    timer->activo = true;
    
    pthread_mutex_lock(&mutex_timers_suspension);
    list_add(timers_suspension, timer);
    pthread_mutex_unlock(&mutex_timers_suspension);
    
    // Crear hilo para el timer
    if (pthread_create(&timer->hilo_timer, NULL, timer_suspension_proceso, timer) != 0) {
        log_error(kernel_logger, "Error al crear hilo timer para PID=%d", pid);
        
        pthread_mutex_lock(&mutex_timers_suspension);
        list_remove_element(timers_suspension, timer);
        pthread_mutex_unlock(&mutex_timers_suspension);
        
        free(timer);
    } else {
        pthread_detach(timer->hilo_timer);
        log_info(kernel_logger, "Timer de suspensión iniciado para PID=%d (%d ms)", pid, TIEMPO_SUSPENSION);
    }
}

// Función para cancelar timer de suspensión
void cancelar_timer_suspension(int pid) {
    log_trace(kernel_logger, "Cancelando timer de suspensión para proceso PID=%d", pid);
    
    pthread_mutex_lock(&mutex_timers_suspension);
    
    for (int i = 0; i < list_size(timers_suspension); i++) {
        t_timer_suspension* timer = list_get(timers_suspension, i);
        if (timer->pid == pid && timer->activo) {
            timer->activo = false;
            list_remove(timers_suspension, i);
            log_info(kernel_logger, "Timer de suspensión cancelado para PID=%d", pid);
            // No liberamos aquí porque el hilo lo hará
            break;
        }
    }
    
    pthread_mutex_unlock(&mutex_timers_suspension);
}

// Hilo que ejecuta el timer de suspensión por proceso
void* timer_suspension_proceso(void* arg) {
    t_timer_suspension* timer = (t_timer_suspension*)arg;
    int pid = timer->pid;
    
    log_trace(kernel_logger, "Hilo timer iniciado para PID=%d", pid);
    
    // Esperar el tiempo de suspensión
    usleep(TIEMPO_SUSPENSION * 1000); // Convertir ms a microsegundos
    
    // Verificar si el timer sigue activo
    pthread_mutex_lock(&mutex_timers_suspension);
    bool timer_activo = timer->activo;
    pthread_mutex_unlock(&mutex_timers_suspension);
    
    if (timer_activo) {
        log_info(kernel_logger, "Timer de suspensión expirado para PID=%d, iniciando suspensión", pid);
        suspender_proceso_automatico(pid);
    } else {
        log_trace(kernel_logger, "Timer de suspensión cancelado para PID=%d", pid);
    }
    
    // Limpiar timer
    free(timer);
    return NULL;
}

// Función para suspender proceso automáticamente
void suspender_proceso_automatico(int pid) {
    log_info(kernel_logger, "## SUSPENSIÓN AUTOMÁTICA: Suspendiendo proceso PID=%d", pid);
    
    t_proceso_kernel* proceso = NULL;
    (void)proceso; // Evitar warning de variable no usada
    
    // Buscar proceso en cola BLOCKED
    pthread_mutex_lock(&mutex_cola_blocked);
    
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* pcb = list_get(cola_blocked, i);
        if (pcb->pid == pid && pcb->estado == ESTADO_BLOCKED) {
            // Log obligatorio de cambio de estado
            log_cambio_estado(pid, "BLOCKED", "SUSP_BLOCKED");
            
            // Necesitamos buscar el proceso completo
            // Por ahora trabajamos con el PCB
            pcb->estado = ESTADO_SUSP_BLOCKED;
            
            log_info(kernel_logger, "Proceso PID=%d transicionado de BLOCKED a SUSP_BLOCKED", pid);
            
            // Comunicar con memoria para swap out
            // comunicar_swap_out_memoria(proceso);
            
            pthread_mutex_unlock(&mutex_cola_blocked);
            
            // Verificar si hay procesos que pueden ingresar tras liberación
            verificar_memoria_tras_suspension();
            
            return;
        }
    }
    
    pthread_mutex_unlock(&mutex_cola_blocked);
    
    log_warning(kernel_logger, "No se encontró proceso PID=%d en BLOCKED para suspender", pid);
}

// ========== COMUNICACIÓN CON MEMORIA PARA SWAP ==========

// Función para comunicar swap out a memoria
bool comunicar_swap_out_memoria(t_proceso_kernel* proceso) {
    if (proceso == NULL) {
        log_error(kernel_logger, "Proceso NULL en comunicar_swap_out_memoria");
        return false;
    }
    
    log_info(kernel_logger, "Solicitando SWAP OUT a memoria para proceso %s (PID=%d)", 
             proceso->nombre, proceso->pcb.pid);
    
    // En implementación real, aquí se haría comunicación por sockets con memoria
    // Enviar mensaje SWAP_OUT con PID del proceso
    // Por ahora simulamos
    usleep(200000); // 200ms simulación
    
    log_info(kernel_logger, "SWAP OUT completado para proceso %s (PID=%d) - memoria liberada", 
             proceso->nombre, proceso->pcb.pid);
    
    return true;
}

// Función para comunicar swap in a memoria
bool comunicar_swap_in_memoria(t_proceso_kernel* proceso) {
    if (proceso == NULL) {
        log_error(kernel_logger, "Proceso NULL en comunicar_swap_in_memoria");
        return false;
    }
    
    log_info(kernel_logger, "Solicitando SWAP IN a memoria para proceso %s (PID=%d)", 
             proceso->nombre, proceso->pcb.pid);
    
    // En implementación real, aquí se haría comunicación por sockets con memoria
    // Enviar mensaje SWAP_IN con PID del proceso
    // Por ahora simulamos
    usleep(300000); // 300ms simulación (más lento que swap out)
    
    log_info(kernel_logger, "SWAP IN completado para proceso %s (PID=%d) - proceso en memoria", 
             proceso->nombre, proceso->pcb.pid);
    
    return true;
}

// ========== VERIFICACIÓN DE MEMORIA Y PROCESAMIENTO DE COLAS ==========

// Función para verificar memoria disponible tras suspensión
void verificar_memoria_tras_suspension() {
    log_trace(kernel_logger, "Verificando memoria disponible tras suspensiones...");
    
    // En implementación real, consultaríamos a memoria por espacio disponible
    // Por ahora simulamos que siempre hay memoria disponible tras suspensión
    bool memoria_disponible = true;
    
    if (memoria_disponible) {
        // Primero procesar SUSP_READY (tiene prioridad sobre NEW)
        if (hay_procesos_susp_ready()) {
            log_trace(kernel_logger, "Hay procesos SUSP_READY esperando, procesando...");
            activar_procesos_susp_ready();
        }
        
        // Luego procesar NEW si no hay SUSP_READY
        else {
            pthread_mutex_lock(&mutex_cola_new);
            int procesos_new = list_size(cola_new);
            pthread_mutex_unlock(&mutex_cola_new);
            
            if (procesos_new > 0) {
                log_trace(kernel_logger, "Procesando cola NEW tras liberación de memoria");
                // Aquí llamaríamos a función del planificador de largo plazo
                // procesar_cola_new_tras_liberacion();
            }
        }
    }
}

// ========== MANEJO DE PROCESOS SUSP_READY ==========

// Función para verificar si hay procesos SUSP_READY
bool hay_procesos_susp_ready() {
    pthread_mutex_lock(&mutex_cola_susp_ready);
    bool hay_procesos = !list_is_empty(cola_susp_ready);
    pthread_mutex_unlock(&mutex_cola_susp_ready);
    
    return hay_procesos;
}

// Función para priorizar SUSP_READY sobre NEW
void priorizar_susp_ready_sobre_new() {
    if (hay_procesos_susp_ready()) {
        log_trace(kernel_logger, "## PRIORIDAD: Hay procesos SUSP_READY, bloqueando ingreso de NEW");
        
        // En implementación real, aquí bloquearíamos el procesamiento de NEW
        // hasta que se vacíe SUSP_READY
        
        pthread_mutex_lock(&mutex_cola_susp_ready);
        int procesos_susp = list_size(cola_susp_ready);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        
        pthread_mutex_lock(&mutex_cola_new);
        int procesos_new = list_size(cola_new);
        pthread_mutex_unlock(&mutex_cola_new);
        
        log_info(kernel_logger, "## PRIORIDAD ACTIVA: %d procesos SUSP_READY esperando, %d procesos NEW bloqueados", 
                 procesos_susp, procesos_new);
    }
}

// Función para activar procesos SUSP_READY
void activar_procesos_susp_ready() {
    pthread_mutex_lock(&mutex_cola_susp_ready);
    
    if (!list_is_empty(cola_susp_ready)) {
        // Activar un proceso por vez para no sobrecargar memoria
        t_proceso_kernel* proceso = list_remove(cola_susp_ready, 0);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        
        if (proceso != NULL) {
            log_info(kernel_logger, "## ACTIVANDO PROCESO SUSPENDIDO: %s (PID=%d)", 
                     proceso->nombre, proceso->pcb.pid);
            
            // Comunicar swap in a memoria
            if (comunicar_swap_in_memoria(proceso)) {
                // Cambiar estado y mover a READY
                // Log obligatorio de cambio de estado
                log_cambio_estado(proceso->pcb.pid, "SUSP_READY", "READY");
                
                proceso->estado = ESTADO_READY;
                proceso->pcb.estado = ESTADO_READY;
                
                pthread_mutex_lock(&mutex_cola_ready);
                list_add(cola_ready, proceso);
                pthread_mutex_unlock(&mutex_cola_ready);
                
                log_info(kernel_logger, "Proceso %s (PID=%d) activado: SUSP_READY → READY", 
                         proceso->nombre, proceso->pcb.pid);
                
            } else {
                // Error en swap in, devolver a SUSP_READY
                log_error(kernel_logger, "Error en SWAP IN para proceso %s, devolviendo a SUSP_READY", 
                          proceso->nombre);
                
                pthread_mutex_lock(&mutex_cola_susp_ready);
                list_add(cola_susp_ready, proceso);
                pthread_mutex_unlock(&mutex_cola_susp_ready);
            }
        }
    } else {
        pthread_mutex_unlock(&mutex_cola_susp_ready);
    }
}

// ========== INTEGRACIÓN CON SISTEMA IO ==========

// Función para procesar fin de IO para procesos suspendidos
void procesar_fin_io_suspendido(int pid) {
    log_info(kernel_logger, "## FIN IO SUSPENDIDO: Procesando fin de IO para proceso PID=%d", pid);
    
    // Buscar proceso en estado SUSP_BLOCKED
    pthread_mutex_lock(&mutex_cola_blocked);
    
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* pcb = list_get(cola_blocked, i);
        if (pcb->pid == pid && pcb->estado == ESTADO_SUSP_BLOCKED) {
            
            log_info(kernel_logger, "Proceso PID=%d encontrado en SUSP_BLOCKED, transicionando a SUSP_READY", pid);
            
            // Transicionar a SUSP_READY
            transicion_susp_blocked_a_susp_ready(pid);
            
            pthread_mutex_unlock(&mutex_cola_blocked);
            return;
        }
    }
    
    pthread_mutex_unlock(&mutex_cola_blocked);
    
    log_warning(kernel_logger, "No se encontró proceso PID=%d en SUSP_BLOCKED", pid);
}

// Función para transición SUSP_BLOCKED → SUSP_READY
void transicion_susp_blocked_a_susp_ready(int pid) {
    pthread_mutex_lock(&mutex_cola_blocked);
    
    t_pcb* pcb_encontrado = NULL;
    int indice = -1;
    
    // Buscar PCB en cola blocked
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* pcb = list_get(cola_blocked, i);
        if (pcb->pid == pid && pcb->estado == ESTADO_SUSP_BLOCKED) {
            pcb_encontrado = pcb;
            indice = i;
            break;
        }
    }
    
    if (pcb_encontrado != NULL && indice != -1) {
        // Remover de BLOCKED
        list_remove(cola_blocked, indice);
        pthread_mutex_unlock(&mutex_cola_blocked);
        
        // Cambiar estado
        pcb_encontrado->estado = ESTADO_SUSP_READY;
        
        // Agregar a SUSP_READY (necesitamos crear t_proceso_kernel)
        // Por simplicidad, creamos estructura mínima
        t_proceso_kernel* proceso = malloc(sizeof(t_proceso_kernel));
        proceso->pcb = *pcb_encontrado;
        proceso->estado = ESTADO_SUSP_READY;
        proceso->nombre = strdup("PROCESO_SUSPENDIDO"); // Placeholder
        
        pthread_mutex_lock(&mutex_cola_susp_ready);
        list_add(cola_susp_ready, proceso);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        
        // Log obligatorio de cambio de estado
        log_cambio_estado(pid, "SUSP_BLOCKED", "SUSP_READY");
        
        log_info(kernel_logger, "## TRANSICIÓN EXITOSA: PID=%d SUSP_BLOCKED → SUSP_READY", pid);
        
    } else {
        pthread_mutex_unlock(&mutex_cola_blocked);
        log_error(kernel_logger, "No se pudo encontrar proceso PID=%d en SUSP_BLOCKED", pid);
    }
}

// ========== FUNCIÓN PARA MANEJAR SUSPENSIÓN (LEGACY) ==========

// Función para manejar suspensión (mantener compatibilidad)
void manejar_suspension(t_proceso_kernel* proceso) {
    if (proceso == NULL) {
        log_error(kernel_logger, "Proceso NULL en manejar_suspension");
        return;
    }
    
    log_info(kernel_logger, "## SUSPENDIENDO PROCESO: %s (PID=%d)", proceso->nombre, proceso->pcb.pid);
    
    // Comunicar swap out a memoria
    if (comunicar_swap_out_memoria(proceso)) {
        proceso->estado = ESTADO_SUSP_BLOCKED;
        proceso->pcb.estado = ESTADO_SUSP_BLOCKED;
        
        log_info(kernel_logger, "Proceso %s suspendido exitosamente", proceso->nombre);
        
        // Verificar si procesos pueden ingresar tras liberación
        verificar_memoria_tras_suspension();
        
    } else {
        log_error(kernel_logger, "Error al suspender proceso %s", proceso->nombre);
    }
}

// Función mejorada para activar proceso suspendido (mantener compatibilidad)
void activar_proceso_suspendido(t_proceso_kernel* proceso) {
    if (proceso == NULL) {
        log_error(kernel_logger, "Proceso NULL en activar_proceso_suspendido");
        return;
    }
    
    log_info(kernel_logger, "## ACTIVANDO PROCESO SUSPENDIDO: %s (PID=%d)", proceso->nombre, proceso->pcb.pid);
    
    // Comunicar swap in a memoria
    if (comunicar_swap_in_memoria(proceso)) {
        
        pthread_mutex_lock(&mutex_cola_susp_ready);
        pthread_mutex_lock(&mutex_cola_ready);
        
        // Remover de SUSP_READY
        list_remove_element(cola_susp_ready, proceso);
        
        // Log obligatorio de cambio de estado
        log_cambio_estado(proceso->pcb.pid, "SUSP_READY", "READY");
        
        // Cambiar estado a READY
        proceso->estado = ESTADO_READY;
        proceso->pcb.estado = ESTADO_READY;
        
        // Agregar a cola READY
        list_add(cola_ready, proceso);
        
        pthread_mutex_unlock(&mutex_cola_ready);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        
        log_info(kernel_logger, "Proceso %s (PID=%d) activado exitosamente: SUSP_READY → READY", 
                 proceso->nombre, proceso->pcb.pid);
        
    } else {
        log_error(kernel_logger, "Error en SWAP IN para proceso %s, manteniéndolo en SUSP_READY", 
                  proceso->nombre);
    }
}