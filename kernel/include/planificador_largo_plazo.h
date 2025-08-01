#ifndef PLANIFICADOR_LARGO_PLAZO_H
#define PLANIFICADOR_LARGO_PLAZO_H
#include <unistd.h>
#include <string.h>
#include "utils_logs.h"
#include "kernel.h"

// Funciones principales del planificador de largo plazo
t_planificador_largo_plazo* iniciar_planificador_largo_plazo();
void* planificar_con_plp(t_proceso_kernel* proceso);
void* planificar_procesos_en_new();
void finalizar_proceso(int pid);

// Funciones auxiliares para manejo de colas
bool comparar_por_tamanio(t_proceso_kernel* p1, t_proceso_kernel* p2);
void agregar_a_lista_ordenado_lp(t_list* lista, t_proceso_kernel* proceso, char* algoritmo);

// Funciones para transiciones de estado
bool intentar_pasar_a_ready(t_proceso_kernel* proceso);
bool consultar_memoria_para_proceso(t_proceso_kernel* proceso);
bool inicializar_proceso_en_memoria(t_proceso_kernel* proceso);
void intentar_procesar_cola_new_pmcp();
void procesar_cola_new_tras_liberacion();

// Funciones de comunicación con memoria
void establecer_conexion_memoria(int fd_conexion);
int obtener_conexion_memoria();
void cerrar_conexion_memoria();

// Funciones para finalización de procesos
bool notificar_finalizacion_a_memoria(t_proceso_kernel* proceso);
void log_metricas_proceso(t_proceso_kernel* proceso);
void liberar_proceso(t_proceso_kernel* proceso);

// Funciones para manejo de historial de estados
void registrar_cambio_estado(t_proceso_kernel* proceso, estado_proceso_t estado_anterior, estado_proceso_t estado_nuevo);
void inicializar_metricas_proceso(t_proceso_kernel* proceso);

// Función auxiliar para búsquedas
bool buscar_por_pid(void* elemento);

#endif // PLANIFICADOR_LARGO_PLAZO_H
