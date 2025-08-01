#include "../include/planificador_corto_plazo.h"
#include <pthread.h>
#include <unistd.h>
#include "../include/utils_logs.h"
#include <commons/log.h>
#include <commons/string.h>
#include <semaphore.h>
#include <pthread.h>
#include "../include/kernel.h"

extern t_list* cola_ready;
extern pthread_mutex_t mutex_cola_ready;
extern float ALFA;
extern int ESTIMACION_INICIAL;
extern char* ALGORITMO_CORTO_PLAZO;


void agregar_a_lista_ordenado_cp(t_list* lista, t_proceso_kernel* proceso, char* algoritmo){
    if (string_equals_ignore_case(algoritmo, "FIFO")) {
        list_add(lista, proceso);
    } else if (string_equals_ignore_case(algoritmo, "SJF")) {
        list_add_sorted(lista, proceso, comparar_procesos_por_estimacion);
    } else if (string_equals_ignore_case(algoritmo, "SRT")) {
        list_add_sorted(lista, proceso, comparar_procesos_por_estimacion);
    }
}

void* hilo_planificador_corto(void* args) {
    (void)args; // Evitar warning de parámetro no usado
    log_info(kernel_logger, "## Iniciando planificador de corto plazo");
    while (1) {
        // Esperar que haya procesos en READY
        sem_wait(&hay_procesos_ready);
        
        pthread_mutex_lock(&mutex_cola_ready);
        if (!list_is_empty(cola_ready)) {
            t_proceso_kernel* proceso = list_remove(cola_ready, 0);
            pthread_mutex_unlock(&mutex_cola_ready);
            
            // Log obligatorio de cambio de estado READY -> EXEC
            log_cambio_estado(proceso->pcb.pid, "READY", "EXEC");
            
            // Aquí se asignaría el proceso a una CPU libre
            // Por ahora solo loggeamos
            log_info(kernel_logger, "## Proceso (%s) asignado para ejecución", proceso->nombre);
            proceso->estado = ESTADO_EXEC;
            
            // Agregar a cola EXEC
            pthread_mutex_lock(&mutex_cola_exec);
            list_add(cola_exec, proceso);
            pthread_mutex_unlock(&mutex_cola_exec);
        } else {
            pthread_mutex_unlock(&mutex_cola_ready);
        }
        
        sleep(1); // Simular tiempo de procesamiento
    }
    return NULL;
}

