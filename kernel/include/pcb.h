#ifndef PCB_H
#define PCB_H

#include <commons/collections/list.h>
#include <commons/collections/queue.h>
#include <commons/log.h>
#include <conexion.h>  // Para las definiciones de t_pcb y estado_proceso_t

// Comparadores
bool comparar_por_estimacion(void* a, void* b);  // Para SJF/SRT con PCBs
bool comparar_procesos_por_estimacion(void* a, void* b);  // Para SJF/SRT con t_proceso_kernel
bool comparar_por_memoria(void* a, void* b);     // Para PMCP

// Logs y estados
char* estado_str(estado_proceso_t estado);

#endif // PCB_H