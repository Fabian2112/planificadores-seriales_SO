#ifndef PLANIFICADOR_CORTO_PLAZO_H
#define PLANIFICADOR_CORTO_PLAZO_H

#include <pthread.h>
#include "kernel.h"
#include "pcb.h"

/**
 * Hilo del planificador de corto plazo. Se encarga de:
 *  - Esperar procesos en READY
 *  - Asignar una CPU libre
 *  - Cambiar el estado del proceso a EXEC
 *  - Enviar el PCB a la CPU
 */
void* hilo_planificador_corto(void* args);
void agregar_a_lista_ordenado_cp(t_list* lista, t_proceso_kernel* proceso, char* algoritmo);
bool comparar_por_estimacion(void* a, void* b);

#endif // PLANIFICADOR_CORTO_PLAZO_H