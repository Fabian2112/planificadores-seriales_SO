#define _POSIX_C_SOURCE 200809L
#include "../include/planificador_largo_plazo.h"
#include "../include/kernel.h"
#include "../include/pcb.h"
#include <commons/log.h>
#include <commons/string.h>
#include <commons/temporal.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <string.h>

// Estructura para registrar cambios de estado
typedef struct {
    estado_proceso_t estado_anterior;
    estado_proceso_t estado_nuevo;
    char* timestamp;
    int duracion_ms; // Duración en el estado anterior
} t_cambio_estado;

extern t_log* kernel_logger;
extern char* ALGORITMO_INGRESO_A_READY;
extern char* ALGORITMO_CORTO_PLAZO;
extern char* IP_MEMORIA;
extern int PUERTO_MEMORIA;
extern pthread_mutex_t mutex_cola_new;
extern pthread_mutex_t mutex_cola_ready;
extern pthread_mutex_t mutex_cola_susp_ready;
extern pthread_mutex_t mutex_cola_exec;
extern t_list* cola_new;
extern t_list* cola_ready;
extern t_list* cola_susp_ready;
extern t_list* cola_exec;
extern t_planificador_largo_plazo* plani_lp;
extern sem_t hay_procesos_ready;

// Variable global para conexión con memoria
static int fd_memoria_global = -1;

// Funciones auxiliares para historial de estados
void registrar_cambio_estado(t_proceso_kernel* proceso, estado_proceso_t estado_anterior, estado_proceso_t estado_nuevo) {
    if (!proceso->pcb.metricas_estado) {
        proceso->pcb.metricas_estado = queue_create();
    }
    
    t_cambio_estado* cambio = malloc(sizeof(t_cambio_estado));
    cambio->estado_anterior = estado_anterior;
    cambio->estado_nuevo = estado_nuevo;
    cambio->timestamp = temporal_get_string_time("%H:%M:%S:%MS");
    
    // Calcular duración en el estado anterior (simplificado por ahora)
    cambio->duracion_ms = 0; // TODO: Implementar cálculo real de duración
    
    queue_push(proceso->pcb.metricas_estado, cambio);
    
    // Log obligatorio de cambio de estado
    log_cambio_estado(proceso->pcb.pid, estado_str(estado_anterior), estado_str(estado_nuevo));
    
    log_info(kernel_logger, "## (%s) - [%s] Estado: %s → %s", 
             proceso->nombre, cambio->timestamp, 
             estado_str(estado_anterior), estado_str(estado_nuevo));
}

void inicializar_metricas_proceso(t_proceso_kernel* proceso) {
    if (!proceso->pcb.metricas_estado) {
        proceso->pcb.metricas_estado = queue_create();
    }
    if (!proceso->pcb.metricas_tiempo) {
        proceso->pcb.metricas_tiempo = queue_create();
    }
    
    // Registrar estado inicial
    registrar_cambio_estado(proceso, ESTADO_NEW, ESTADO_NEW);
}

// Función para establecer la conexión global con memoria desde el main
void establecer_conexion_memoria(int fd_conexion) {
    fd_memoria_global = fd_conexion;
    log_info(kernel_logger, "## Conexión con memoria reutilizada - FD: %d", fd_memoria_global);
}

// Función para obtener conexión con memoria
int obtener_conexion_memoria() {
    if (fd_memoria_global == -1) {
        log_error(kernel_logger, "## Error: No hay conexión con memoria establecida");
        return -1;
    }
    
    return fd_memoria_global;
}

// Función para cerrar la conexión con memoria
void cerrar_conexion_memoria() {
    if (fd_memoria_global != -1) {
        close(fd_memoria_global);
        fd_memoria_global = -1;
        log_info(kernel_logger, "## Conexión con memoria cerrada");
    }
}

// Función auxiliar interna que NO toma mutexes (para evitar deadlock)
bool intentar_pasar_a_ready_interno(t_proceso_kernel* proceso) {
    log_info(kernel_logger, "## (%s) - Consultando memoria para carga de %d bytes", proceso->nombre, proceso->tamanio);
    
    // Consultar disponibilidad de espacio en memoria
    bool espacio_disponible = consultar_memoria_para_proceso(proceso);
    
    if(espacio_disponible) {
        // Si hay espacio, inicializar el proceso en memoria
        bool inicializacion_exitosa = inicializar_proceso_en_memoria(proceso);
        
        if (inicializacion_exitosa) {
            log_info(kernel_logger, "## (%s) - Inicialización exitosa, moviendo a READY", proceso->nombre);
            
            // DEBUG: NO tomar mutex aquí - se asume que ya está tomado por el caller
            log_info(kernel_logger, "## DEBUG: Removiendo proceso %s de cola NEW (mutex ya tomado)", proceso->nombre);
            list_remove_element(cola_new, proceso);
            log_info(kernel_logger, "## (%s) - Removido de cola NEW", proceso->nombre);
            
            // Registrar cambio de estado ANTES de modificar el estado
            estado_proceso_t estado_anterior = proceso->estado;
            log_info(kernel_logger, "## (%s) - Estado anterior: %s", proceso->nombre, estado_str(estado_anterior));
            
            // Cambiar estado a READY
            proceso->estado = ESTADO_READY;
            proceso->pcb.estado = ESTADO_READY;
            
            log_info(kernel_logger, "## DEBUG: Estado cambiado, agregando a cola READY para proceso %s", proceso->nombre);
            
            // Agregar a cola READY
            pthread_mutex_lock(&mutex_cola_ready);
            list_add(cola_ready, proceso);
            pthread_mutex_unlock(&mutex_cola_ready);
            
            log_info(kernel_logger, "## (%s) - Proceso agregado a cola READY exitosamente", proceso->nombre);
            
            // Registrar cambio de estado
            registrar_cambio_estado(proceso, estado_anterior, ESTADO_READY);
            
            // Señalizar que hay procesos ready para el planificador de corto plazo
            log_info(kernel_logger, "## (%s) - Publicando semáforo hay_procesos_ready", proceso->nombre);
            sem_post(&hay_procesos_ready);
            
            log_info(kernel_logger, "## (%s) - Estado Anterior: %s - Estado Actual: READY", 
                     proceso->nombre, estado_str(estado_anterior));
            return true;
        } else {
            log_error(kernel_logger, "## (%s) - Error al inicializar proceso en memoria", proceso->nombre);
            return false;
        }
    } else {
        log_info(kernel_logger, "## (%s) - No hay espacio en memoria (%d bytes), permanece en NEW", proceso->nombre, proceso->tamanio);
        return false;
    }
}

// Función auxiliar para intentar pasar un proceso a READY
bool intentar_pasar_a_ready(t_proceso_kernel* proceso) {
    log_info(kernel_logger, "## (%s) - Consultando memoria para carga de %d bytes", proceso->nombre, proceso->tamanio);
    
    // Consultar disponibilidad de espacio en memoria
    bool espacio_disponible = consultar_memoria_para_proceso(proceso);
    
    if(espacio_disponible) {
        // Si hay espacio, inicializar el proceso en memoria
        bool inicializacion_exitosa = inicializar_proceso_en_memoria(proceso);
        
        if (inicializacion_exitosa) {
            log_info(kernel_logger, "## (%s) - Inicialización exitosa, moviendo a READY", proceso->nombre);
            
            // DEBUG: Logging antes de cada operación crítica
            log_info(kernel_logger, "## DEBUG: Iniciando remoción de cola NEW para proceso %s", proceso->nombre);
            
            log_info(kernel_logger, "## DEBUG: Intentando obtener mutex_cola_new...");
            
            // Usar trylock para evitar deadlock indefinido
            int mutex_result = pthread_mutex_trylock(&mutex_cola_new);
            if (mutex_result != 0) {
                log_error(kernel_logger, "## ERROR: No se pudo obtener mutex_cola_new (resultado: %d) - Posible deadlock", mutex_result);
                
                // Intentar con timeout
                log_info(kernel_logger, "## DEBUG: Esperando mutex_cola_new con timeout...");
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 2; // 2 segundos de timeout
                
                mutex_result = pthread_mutex_timedlock(&mutex_cola_new, &timeout);
                if (mutex_result != 0) {
                    log_error(kernel_logger, "## ERROR CRÍTICO: Timeout en mutex_cola_new - No se puede remover proceso %s", proceso->nombre);
                    return false;
                }
            }
            
            log_info(kernel_logger, "## DEBUG: Mutex cola_new obtenido exitosamente, removiendo proceso %s", proceso->nombre);
            list_remove_element(cola_new, proceso);
            pthread_mutex_unlock(&mutex_cola_new);
            log_info(kernel_logger, "## DEBUG: Mutex cola_new liberado, proceso %s removido de cola NEW", proceso->nombre);
            
            // Registrar cambio de estado ANTES de modificar el estado
            estado_proceso_t estado_anterior = proceso->estado;
            log_info(kernel_logger, "## (%s) - Estado anterior: %s", proceso->nombre, estado_str(estado_anterior));
            
            // DEBUG: Logging antes de cambio de estado
            log_info(kernel_logger, "## DEBUG: Cambiando estado de %s a READY", proceso->nombre);
            
            // Cambiar estado a READY
            proceso->estado = ESTADO_READY;
            proceso->pcb.estado = ESTADO_READY;
            
            log_info(kernel_logger, "## DEBUG: Estado cambiado, agregando a cola READY para proceso %s", proceso->nombre);
            
            // Agregar a cola READY
            pthread_mutex_lock(&mutex_cola_ready);
            list_add(cola_ready, proceso);
            pthread_mutex_unlock(&mutex_cola_ready);
            
            log_info(kernel_logger, "## (%s) - Proceso agregado a cola READY exitosamente", proceso->nombre);
            
            // Registrar cambio de estado
            registrar_cambio_estado(proceso, estado_anterior, ESTADO_READY);
            
            // Señalizar que hay procesos ready para el planificador de corto plazo
            log_info(kernel_logger, "## (%s) - Publicando semáforo hay_procesos_ready", proceso->nombre);
            sem_post(&hay_procesos_ready);
            
            log_info(kernel_logger, "## (%s) - Estado Anterior: %s - Estado Actual: READY", 
                     proceso->nombre, estado_str(estado_anterior));
            return true;
        } else {
            log_error(kernel_logger, "## (%s) - Error al inicializar proceso en memoria", proceso->nombre);
            return false;
        }
    } else {
        log_info(kernel_logger, "## (%s) - No hay espacio en memoria (%d bytes), permanece en NEW", proceso->nombre, proceso->tamanio);
        return false;
    }
}

// Función para inicializar un proceso en memoria (crear estructuras, tablas de páginas, etc.)
// Función para inicializar un proceso en memoria (crear estructuras, tablas de páginas, etc.)
bool inicializar_proceso_en_memoria(t_proceso_kernel* proceso) {
    int fd_memoria = obtener_conexion_memoria();
    if (fd_memoria == -1) {
        log_error(kernel_logger, "## Error: No hay conexión con memoria para inicialización");
        return false;
    }
    
    log_info(kernel_logger, "## Inicializando proceso en memoria - PID: %d - Tamaño: %d bytes - Nombre: %s", 
             proceso->pcb.pid, proceso->tamanio, proceso->nombre);
    
    // Crear paquete para creación de proceso
    t_paquete* paquete = crear_paquete(CREAR_PROCESO, kernel_logger);
    if (!paquete) {
        log_error(kernel_logger, "## Error: No se pudo crear paquete para creación");
        return false;
    }
    
    // Agregar PID del proceso
    agregar_a_paquete(paquete, &(proceso->pcb.pid), sizeof(int));
    
    // Agregar tamaño del proceso
    agregar_a_paquete(paquete, &(proceso->tamanio), sizeof(int));
    
    // Agregar nombre del proceso
    int longitud_nombre = strlen(proceso->nombre);
    agregar_a_paquete(paquete, &longitud_nombre, sizeof(int));
    agregar_a_paquete(paquete, proceso->nombre, longitud_nombre);
    
    // Enviar paquete
    enviar_paquete(paquete, fd_memoria);
    eliminar_paquete(paquete);
    
    log_info(kernel_logger, "## Paquete CREAR_PROCESO enviado - PID: %d, Tamaño: %d, Nombre: %s", 
             proceso->pcb.pid, proceso->tamanio, proceso->nombre);
    
    // Recibir confirmación de memoria
    bool confirmacion_creacion = false;
    ssize_t bytes_recibidos = recv(fd_memoria, &confirmacion_creacion, sizeof(bool), MSG_WAITALL);
    
    if (bytes_recibidos != sizeof(bool)) {
        log_error(kernel_logger, "## Error: No se pudo recibir confirmación de creación");
        return false;
    }
    
    if (confirmacion_creacion) {
        log_info(kernel_logger, "## Memoria confirmó creación exitosa del proceso %s (PID: %d)", 
                 proceso->nombre, proceso->pcb.pid);
    } else {
        log_error(kernel_logger, "## Memoria reportó error en creación del proceso %s (PID: %d)", 
                  proceso->nombre, proceso->pcb.pid);
    }
    
    return confirmacion_creacion;
}

// Función para consultar a memoria si hay espacio disponible
bool consultar_memoria_para_proceso(t_proceso_kernel* proceso) {
    int fd_memoria = obtener_conexion_memoria();
    if (fd_memoria == -1) {
        log_error(kernel_logger, "## Error: No hay conexión con memoria para consulta");
        return false;
    }
    
    log_info(kernel_logger, "## Consultando espacio en memoria para proceso PID: %d - Tamaño: %d bytes", 
             proceso->pcb.pid, proceso->tamanio);
    
    // Crear paquete para consulta de espacio
    t_paquete* paquete = crear_paquete(CONSULTAR_ESPACIO, kernel_logger);
    if (!paquete) {
        log_error(kernel_logger, "## Error: No se pudo crear paquete para consulta");
        return false;
    }
    
    // Agregar PID del proceso
    agregar_a_paquete(paquete, &(proceso->pcb.pid), sizeof(int));
    
    // Agregar tamaño requerido
    agregar_a_paquete(paquete, &(proceso->tamanio), sizeof(int));
    
    // Agregar nombre del proceso
    int longitud_nombre = strlen(proceso->nombre);
    agregar_a_paquete(paquete, &longitud_nombre, sizeof(int));
    agregar_a_paquete(paquete, proceso->nombre, longitud_nombre);
    
    // Enviar paquete
    enviar_paquete(paquete, fd_memoria);
    eliminar_paquete(paquete);
    
    log_info(kernel_logger, "## Paquete CONSULTAR_ESPACIO enviado - PID: %d, Tamaño: %d, Nombre: %s", 
             proceso->pcb.pid, proceso->tamanio, proceso->nombre);
    
    log_info(kernel_logger, "## Esperando respuesta de memoria...");
    
    // Recibir respuesta de memoria
    bool respuesta_disponible = false;
    ssize_t bytes_recibidos = recv(fd_memoria, &respuesta_disponible, sizeof(bool), MSG_WAITALL);
    
    log_info(kernel_logger, "## Bytes recibidos de memoria: %zd (esperados: %zu)", bytes_recibidos, sizeof(bool));
    
    log_info(kernel_logger, "## Bytes recibidos de memoria: %zd (esperados: %zu)", bytes_recibidos, sizeof(bool));
    
    if (bytes_recibidos != sizeof(bool)) {
        log_error(kernel_logger, "## Error: No se pudo recibir respuesta de memoria (bytes: %zd, errno: %d - %s)", 
                  bytes_recibidos, errno, strerror(errno));
        return false;
    }
    
    log_info(kernel_logger, "## Respuesta recibida de memoria: %s", respuesta_disponible ? "ESPACIO_DISPONIBLE" : "SIN_ESPACIO");
    
    if (respuesta_disponible) {
        log_info(kernel_logger, "## Memoria confirmó disponibilidad de espacio para proceso %s", proceso->nombre);
    } else {
        log_info(kernel_logger, "## Memoria indica que no hay espacio suficiente para proceso %s", proceso->nombre);
    }
    
    return respuesta_disponible;
}

t_planificador_largo_plazo* iniciar_planificador_largo_plazo(){
    t_planificador_largo_plazo* plani_lp = malloc(sizeof(t_planificador_largo_plazo));
    plani_lp->estado = strdup("STOP");
    plani_lp->activo = false;
    return plani_lp;
}

bool comparar_por_tamanio(t_proceso_kernel* p1, t_proceso_kernel* p2) {
    return p1->tamanio < p2->tamanio;
}

void agregar_a_lista_ordenado_lp(t_list* lista, t_proceso_kernel* proceso, char* algoritmo){
    if (string_equals_ignore_case(algoritmo, "FIFO")) {
        list_add(lista, proceso);
        log_info(kernel_logger, "## (%s) - Agregado al final de cola (FIFO)", proceso->nombre);
    } else if (string_equals_ignore_case(algoritmo, "PMCP")) {
        list_add_sorted(lista, proceso, (void*)comparar_por_tamanio);
        log_info(kernel_logger, "## (%s) - Agregado ordenado por tamaño (%d bytes) (PMCP)", proceso->nombre, proceso->tamanio);
    }
}

// Función específica para procesar cola NEW con algoritmo PMCP
void intentar_procesar_cola_new_pmcp() {
    // Esta función debe ser llamada con los mutexes ya tomados
    while (!list_is_empty(cola_new)) {
        t_proceso_kernel* proceso_mas_pequeno = list_get(cola_new, 0);
        
        if (intentar_pasar_a_ready_interno(proceso_mas_pequeno)) {
            log_info(kernel_logger, "## (%s) - Continúa procesamiento PMCP", proceso_mas_pequeno->nombre);
        } else {
            // Si el proceso más pequeño no puede pasar, ninguno podrá
            log_info(kernel_logger, "## Procesamiento PMCP detenido - Sin espacio para proceso más pequeño");
            break;
        }
    }
}

// Función para procesar la cola NEW después de liberación de memoria
void procesar_cola_new_tras_liberacion() {
    pthread_mutex_lock(&mutex_cola_new);
    pthread_mutex_lock(&mutex_cola_susp_ready);
    
    // Solo procesar NEW si SUSP_READY está vacía
    if (!list_is_empty(cola_susp_ready)) {
        log_info(kernel_logger, "## Cola SUSP_READY no vacía (%d procesos) - NEW debe esperar", list_size(cola_susp_ready));
        pthread_mutex_unlock(&mutex_cola_new);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        return;
    }
    
    if (list_is_empty(cola_new)) {
        log_info(kernel_logger, "## Cola NEW vacía - Nada que procesar");
        pthread_mutex_unlock(&mutex_cola_new);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        return;
    }
    
    log_info(kernel_logger, "## Procesando cola NEW tras liberación de memoria (%d procesos)", list_size(cola_new));
    
    if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "FIFO")) {
        // FIFO: Intentar pasar procesos en orden hasta que uno falle
        while (!list_is_empty(cola_new)) {
            t_proceso_kernel* primer_proceso = list_get(cola_new, 0);
            
            if (intentar_pasar_a_ready(primer_proceso)) {
                log_info(kernel_logger, "## (%s) - Procesado con FIFO tras liberación", primer_proceso->nombre);
            } else {
                // Si el primero no puede pasar, detener el procesamiento
                log_info(kernel_logger, "## FIFO detenido - Primer proceso no puede pasar a READY");
                break;
            }
        }
    } else if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "PMCP")) {
        // PMCP: Intentar pasar procesos ordenados por tamaño
        intentar_procesar_cola_new_pmcp();
    }
    
    pthread_mutex_unlock(&mutex_cola_new);
    pthread_mutex_unlock(&mutex_cola_susp_ready);
}

void* planificar_con_plp(t_proceso_kernel* proceso) {
    if(string_equals_ignore_case(plani_lp->estado, "INICIADO")) {
        pthread_mutex_lock(&mutex_cola_new);
        pthread_mutex_lock(&mutex_cola_susp_ready);
        
        // Caso 1: Cola NEW vacía y SUSP_READY vacía - Agregar e intentar pasar a READY
        if (list_is_empty(cola_susp_ready) && list_is_empty(cola_new)) {
            agregar_a_lista_ordenado_lp(cola_new, proceso, ALGORITMO_INGRESO_A_READY);
            log_info(kernel_logger, "## (%s) - Agregado a NEW (primera posición)", proceso->nombre);
            
            // Intentar pasar inmediatamente
            if (intentar_pasar_a_ready_interno(proceso)) {
                log_info(kernel_logger, "## (%s) - Pasó inmediatamente a READY", proceso->nombre);
                
                // EJECUTAR PLANIFICADOR DE CORTO PLAZO
                log_info(kernel_logger, "## Activando planificador de corto plazo tras ingreso a READY");
                sem_post(&hay_procesos_ready); // Señalizar que hay procesos ready
            }
        }
        // Caso 2: Cola SUSP_READY vacía pero NEW no vacía - Verificar algoritmo
        else if(list_is_empty(cola_susp_ready) && !list_is_empty(cola_new)){
            if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "FIFO")) {
                // FIFO: Solo agregar a la cola, no intentar pasar hasta que el primero pase
                agregar_a_lista_ordenado_lp(cola_new, proceso, ALGORITMO_INGRESO_A_READY);
                log_info(kernel_logger, "## (%s) - Agregado a NEW (FIFO) - Esperando turno", proceso->nombre);
                
                // En FIFO, solo se puede intentar el primero de la cola
                t_proceso_kernel* primer_proceso = list_get(cola_new, 0);
                if (primer_proceso == proceso) {
                    // Si el proceso recién agregado es el primero, intentar pasarlo
                    if (intentar_pasar_a_ready_interno(proceso)) {
                        log_info(kernel_logger, "## (%s) - Primer proceso FIFO pasó a READY", proceso->nombre);
                        sem_post(&hay_procesos_ready); // Señalizar que hay procesos ready
                    }
                }
            } else if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "PMCP")) {
                // PMCP: Agregar ordenado y intentar pasar si es el más pequeño
                agregar_a_lista_ordenado_lp(cola_new, proceso, ALGORITMO_INGRESO_A_READY);
                log_info(kernel_logger, "## (%s) - Agregado a NEW (PMCP) - Tamaño: %d bytes", proceso->nombre, proceso->tamanio);
                
                // En PMCP, intentar pasar el proceso más pequeño (que debería estar en posición 0)
                t_proceso_kernel* proceso_mas_pequeno = list_get(cola_new, 0);
                if (intentar_pasar_a_ready_interno(proceso_mas_pequeno)) {
                    log_info(kernel_logger, "## (%s) - Proceso más pequeño pasó a READY", proceso_mas_pequeno->nombre);
                    sem_post(&hay_procesos_ready); // Señalizar que hay procesos ready
                    
                    // Continuar intentando con los siguientes hasta que uno falle
                    intentar_procesar_cola_new_pmcp();
                }
            }
        } 
        // Caso 3: Cola SUSP_READY no vacía - Solo agregar a NEW (no pueden pasar hasta que SUSP_READY se vacíe)
        else {
            agregar_a_lista_ordenado_lp(cola_new, proceso, ALGORITMO_INGRESO_A_READY);
            log_info(kernel_logger, "## (%s) - Agregado a NEW - Cola SUSP_READY no vacía (%d procesos esperando)", 
                    proceso->nombre, list_size(cola_susp_ready));
        }
        
        pthread_mutex_unlock(&mutex_cola_new);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
    } else {
        log_warning(kernel_logger, "## Planificador de largo plazo no está iniciado - Proceso %s no agregado", proceso->nombre);
    }
    return NULL;
}

void* planificar_procesos_en_new(){
    while (1) {
        if (plani_lp == NULL) {
            log_error(kernel_logger, "## ERROR: plani_lp es NULL en planificar_procesos_en_new. Esperando inicialización...");
            sleep(1);
            continue;
        }
        if(string_equals_ignore_case(plani_lp->estado, "INICIADO")) {
            pthread_mutex_lock(&mutex_cola_new);
            pthread_mutex_lock(&mutex_cola_susp_ready);
            
            // Solo procesar NEW si SUSP_READY está vacía
            if (list_is_empty(cola_susp_ready)) {
                if(list_is_empty(cola_new)){
                    pthread_mutex_unlock(&mutex_cola_new);
                    pthread_mutex_unlock(&mutex_cola_susp_ready);
                    sleep(1); // Esperar antes de verificar nuevamente
                    continue;
                }
                
                if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "FIFO")) {
                    // FIFO: Intentar pasar el primer proceso (mutex ya tomado)
                    t_proceso_kernel* primer_proceso = list_get(cola_new, 0);
                    intentar_pasar_a_ready_interno(primer_proceso);
                    // Si no pudo pasar, detener procesamiento hasta que se libere espacio
                    if (primer_proceso->estado != ESTADO_READY) {
                        pthread_mutex_unlock(&mutex_cola_new);
                        pthread_mutex_unlock(&mutex_cola_susp_ready);
                        sleep(1);
                        continue;
                    }
                } else if (string_equals_ignore_case(ALGORITMO_INGRESO_A_READY, "PMCP")) {
                    // PMCP: Intentar pasar procesos ordenados por tamaño (mutex ya tomado)
                    bool algun_proceso_paso = false;
                    for (int i = 0; i < list_size(cola_new); i++) {
                        t_proceso_kernel* proceso = list_get(cola_new, i);
                        intentar_pasar_a_ready_interno(proceso);
                        if (proceso->estado == ESTADO_READY) {
                            algun_proceso_paso = true;
                            i--; // Ajustar índice ya que se removió un elemento
                        }
                    }
                    if (!algun_proceso_paso) {
                        pthread_mutex_unlock(&mutex_cola_new);
                        pthread_mutex_unlock(&mutex_cola_susp_ready);
                        sleep(1);
                        continue;
                    }
                }
            }
        
            pthread_mutex_unlock(&mutex_cola_new);
            pthread_mutex_unlock(&mutex_cola_susp_ready);
        }
        sleep(1); // Evitar busy waiting
    }
    return NULL;
}

// Variable temporal para búsqueda
static int pid_a_buscar = -1;

// Función auxiliar para buscar proceso por PID
bool buscar_por_pid(void* elemento) {
    t_proceso_kernel* proc = (t_proceso_kernel*)elemento;
    return proc->pcb.pid == pid_a_buscar;
}

void finalizar_proceso(int pid){
    pthread_mutex_lock(&mutex_cola_exec);
    
    // Configurar PID para búsqueda
    pid_a_buscar = pid;
    
    // Buscar el proceso en la cola EXEC
    t_proceso_kernel* proceso = list_find(cola_exec, buscar_por_pid);
    if (proceso == NULL) {
        log_error(kernel_logger, "## Error: Proceso PID %d no encontrado en cola EXEC", pid);
        pthread_mutex_unlock(&mutex_cola_exec);
        return;
    }
    
    // Remover proceso de cola EXEC
    list_remove_element(cola_exec, proceso);
    pthread_mutex_unlock(&mutex_cola_exec);
    
    // Notificar a memoria la finalización
    log_info(kernel_logger, "## (%s) - Finalizando proceso - PID: %d - Solicitando liberación de %d bytes", 
             proceso->nombre, pid, proceso->tamanio);
    
    bool respuesta_memoria = notificar_finalizacion_a_memoria(proceso);
    
    if(respuesta_memoria) {
        log_info(kernel_logger, "## (%s) - Memoria confirmó liberación de %d bytes", proceso->nombre, proceso->tamanio);
        
        // Registrar cambio de estado a EXIT ANTES de cambiar el estado
        estado_proceso_t estado_anterior = proceso->estado;
        proceso->estado = ESTADO_EXIT;
        registrar_cambio_estado(proceso, estado_anterior, ESTADO_EXIT);
        
        log_info(kernel_logger, "## (%s) - Estado Anterior: %s - Estado Actual: EXIT", 
                 proceso->nombre, estado_str(estado_anterior));
        
        // Loguear métricas completas del proceso
        log_metricas_proceso(proceso);
        
        // ORDEN CRUCIAL: Primero intentar procesos de SUSP_READY, luego NEW
        log_info(kernel_logger, "## Espacio liberado - Verificando colas para nuevos ingresos");
        
        // 1. Primero verificar SUSP_READY (planificador de mediano plazo)
        pthread_mutex_lock(&mutex_cola_susp_ready);
        int procesos_susp_ready = list_size(cola_susp_ready);
        pthread_mutex_unlock(&mutex_cola_susp_ready);
        
        if (procesos_susp_ready > 0) {
            log_info(kernel_logger, "## Hay %d procesos en SUSP_READY - Procesando con planificador mediano plazo", procesos_susp_ready);
            // TODO: Llamar al planificador de mediano plazo
            // planificar_procesos_en_susp_ready();
        } else {
            log_info(kernel_logger, "## Cola SUSP_READY vacía - Verificando cola NEW");
            
            // 2. Solo si SUSP_READY está vacía, procesar NEW
            pthread_mutex_lock(&mutex_cola_new);
            int procesos_new = list_size(cola_new);
            pthread_mutex_unlock(&mutex_cola_new);
            
            if (procesos_new > 0) {
                log_info(kernel_logger, "## Procesando %d procesos de cola NEW", procesos_new);
                procesar_cola_new_tras_liberacion();
            } else {
                log_info(kernel_logger, "## Ambas colas NEW y SUSP_READY están vacías");
            }
        }
        
        // Liberar memoria del proceso
        log_info(kernel_logger, "## Finalizado: (%s) - PID: %d - Memoria liberada", proceso->nombre, pid);
        liberar_proceso(proceso);
        
    } else {
        log_error(kernel_logger, "## Error: Memoria no confirmó liberación para proceso %d", pid);
        // En caso de error, devolver el proceso a EXEC
        pthread_mutex_lock(&mutex_cola_exec);
        list_add(cola_exec, proceso);
        pthread_mutex_unlock(&mutex_cola_exec);
    }
}

// Función auxiliar para notificar finalización a memoria
// Función auxiliar para notificar finalización a memoria
bool notificar_finalizacion_a_memoria(t_proceso_kernel* proceso) {
    int fd_memoria = obtener_conexion_memoria();
    if (fd_memoria == -1) {
        log_error(kernel_logger, "## Error: No hay conexión con memoria para finalización");
        return false;
    }
    
    log_info(kernel_logger, "## Notificando finalización a memoria - PID: %d - Liberando %d bytes", 
             proceso->pcb.pid, proceso->tamanio);
    
    // Crear paquete para liberación de espacio
    t_paquete* paquete = crear_paquete(LIBERAR_ESPACIO, kernel_logger);
    if (!paquete) {
        log_error(kernel_logger, "## Error: No se pudo crear paquete para liberación");
        return false;
    }
    
    // Agregar PID del proceso
    agregar_a_paquete(paquete, &(proceso->pcb.pid), sizeof(int));
    
    // Agregar tamaño a liberar (para verificación/logging en memoria)
    agregar_a_paquete(paquete, &(proceso->tamanio), sizeof(int));
    
    // Agregar nombre del proceso (para logging)
    int longitud_nombre = strlen(proceso->nombre);
    agregar_a_paquete(paquete, &longitud_nombre, sizeof(int));
    agregar_a_paquete(paquete, proceso->nombre, longitud_nombre);
    
    // Enviar paquete
    enviar_paquete(paquete, fd_memoria);
    eliminar_paquete(paquete);
    
    log_info(kernel_logger, "## Paquete LIBERAR_ESPACIO enviado - PID: %d, Tamaño: %d, Nombre: %s", 
             proceso->pcb.pid, proceso->tamanio, proceso->nombre);
    
    // Recibir confirmación de memoria
    bool confirmacion_liberacion = false;
    ssize_t bytes_recibidos = recv(fd_memoria, &confirmacion_liberacion, sizeof(bool), MSG_WAITALL);
    
    if (bytes_recibidos != sizeof(bool)) {
        log_error(kernel_logger, "## Error: No se pudo recibir confirmación de liberación de memoria");
        return false;
    }
    
    if (confirmacion_liberacion) {
        log_info(kernel_logger, "## Memoria confirmó liberación exitosa para proceso %s (PID: %d)", 
                 proceso->nombre, proceso->pcb.pid);
    } else {
        log_error(kernel_logger, "## Memoria reportó error en liberación del proceso %s (PID: %d)", 
                  proceso->nombre, proceso->pcb.pid);
    }
    
    return confirmacion_liberacion;
}

// Función auxiliar para loguear métricas del proceso
void log_metricas_proceso(t_proceso_kernel* proceso) {
    log_info(kernel_logger, "## === MÉTRICAS PROCESO ===");
    log_info(kernel_logger, "## Proceso: %s", proceso->nombre);
    log_info(kernel_logger, "## PID: %d", proceso->pcb.pid);
    log_info(kernel_logger, "## Tamaño: %d bytes", proceso->tamanio);
    log_info(kernel_logger, "## Estado final: EXIT");
    
    // Implementar logging completo del historial de estados
    if (proceso->pcb.metricas_estado && !queue_is_empty(proceso->pcb.metricas_estado)) {
        log_info(kernel_logger, "## === HISTORIAL DE ESTADOS ===");
        
        // Crear una copia de la cola para no modificar la original
        t_queue* cola_copia = queue_create();
        int contador = 1;
        
        // Copiar elementos a una cola temporal
        while (!queue_is_empty(proceso->pcb.metricas_estado)) {
            t_cambio_estado* cambio = queue_pop(proceso->pcb.metricas_estado);
            queue_push(cola_copia, cambio);
        }
        
        // Procesar y mostrar el historial, restaurando la cola original
        while (!queue_is_empty(cola_copia)) {
            t_cambio_estado* cambio = queue_pop(cola_copia);
            
            if (cambio->estado_anterior == cambio->estado_nuevo) {
                // Estado inicial
                log_info(kernel_logger, "## %d. [%s] Estado inicial: %s", 
                         contador, cambio->timestamp, estado_str(cambio->estado_nuevo));
            } else {
                // Transición de estado
                log_info(kernel_logger, "## %d. [%s] Transición: %s → %s (Duración anterior: %d ms)", 
                         contador, cambio->timestamp, 
                         estado_str(cambio->estado_anterior), 
                         estado_str(cambio->estado_nuevo),
                         cambio->duracion_ms);
            }
            
            contador++;
            queue_push(proceso->pcb.metricas_estado, cambio); // Restaurar a la cola original
        }
        
        queue_destroy(cola_copia);
        log_info(kernel_logger, "## Total de transiciones: %d", contador - 1);
    } else {
        log_warning(kernel_logger, "## Sin historial de estados disponible");
    }
    
    // Log obligatorio de métricas en formato requerido
    log_metricas_estado(proceso->pcb.pid);
    
    log_info(kernel_logger, "## ========================");
}

// Función auxiliar para liberar completamente un proceso
void liberar_proceso(t_proceso_kernel* proceso) {
    // Liberar el nombre si fue asignado dinámicamente
    if (proceso->nombre) {
        free(proceso->nombre);
    }
    
    // Liberar las instrucciones si las hay
    if (proceso->codigo) {
        list_destroy_and_destroy_elements(proceso->codigo, free);
    }
    
    // Liberar las métricas del PCB
    if (proceso->pcb.metricas_tiempo) {
        queue_destroy(proceso->pcb.metricas_tiempo);
    }
    
    // Liberar el historial de estados
    if (proceso->pcb.metricas_estado) {
        // Liberar cada cambio de estado
        while (!queue_is_empty(proceso->pcb.metricas_estado)) {
            t_cambio_estado* cambio = queue_pop(proceso->pcb.metricas_estado);
            if (cambio->timestamp) {
                free(cambio->timestamp);
            }
            free(cambio);
        }
        queue_destroy(proceso->pcb.metricas_estado);
    }
    
    // Liberar el proceso
    free(proceso);
}
