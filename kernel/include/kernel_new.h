#ifndef KERNEL_MAIN_H
#define KERNEL_MAIN_H

#include <commons/collections/list.h>
#include <commons/log.h>
#include <commons/config.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/socket.h>
#include <string.h>
#include "pcb.h"

// Constantes específicas del kernel
#define CONSULTAR_ESPACIO 100
#define LIBERAR_ESPACIO 101
#define CREAR_PROCESO 3
#define FINALIZAR_PROCESO 4
#define ENVIO_ARCHIVO_KERNEL 2
#define HANDSHAKE 200
#define MODULO_KERNEL 0
#define SYSCALL 42
#define SYSCALL_OK 43
#define SYSCALL_ERROR 44
#define PCB_DEVUELTO 201
#define PCB_A_EJECUTAR 202

// Tipos necesarios para comunicación
typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
	int codigo_operacion;
	t_buffer* buffer;
} t_paquete;

// Tipo para operaciones IO
typedef struct {
    char* dispositivo;
    int tiempo_ms;
    int pid_proceso;
    pthread_t hilo_io;
    bool completada;
} t_operacion_io;

// Declaraciones de funciones externas
int crear_conexion(t_log* logger, char *ip, int puerto);
int iniciar_servidor(t_log* logger, int puerto);
int esperar_cliente(t_log* logger, int servidor);
bool string_equals_ignore_case(char* str1, char* str2);

// Logger y Config
extern t_log* kernel_logger;
extern t_config* kernel_config;

// Variables de configuración
extern char* IP_MEMORIA;
extern int PUERTO_MEMORIA;

extern int PUERTO_ESCUCHA_DISPATCH;
extern int PUERTO_ESCUCHA_INTERRUPT;
extern int PUERTO_ESCUCHA_IO;

extern t_list* cola_new;
extern t_list* cola_ready;
extern t_list* cola_susp_ready;
extern t_list* cola_exec;
extern t_list* operaciones_io_activas;

extern char* ALGORITMO_PLANIFICACION;
extern char* ALGORITMO_INGRESO_A_READY;
extern char* ALGORITMO_CORTO_PLAZO;
extern float ALFA;
extern int ESTIMACION_INICIAL;
extern int TIEMPO_SUSPENSION;
extern char* LOG_LEVEL;

extern int fd_kernel_dispatch;
extern int fd_cpu_dispatch;
extern int fd_cpu_interrupt;
extern int conexiones;

// Mutexes para las colas
extern pthread_mutex_t mutex_cola_new;
extern pthread_mutex_t mutex_cola_ready;
extern pthread_mutex_t mutex_cola_susp_ready;
extern pthread_mutex_t mutex_cola_exec;
extern pthread_mutex_t mutex_cola_blocked;
extern pthread_mutex_t mutex_syscall_kernel;
extern pthread_mutex_t mutex_io_activas;
extern pthread_mutex_t mutex_dispositivos_io;
extern pthread_mutex_t mutex_timers_suspension;

// Semáforos
extern sem_t sem_archivo_listo;
extern sem_t sem_servidor_cpu_listo;
extern sem_t sem_kernel_cpu_hs;
extern sem_t sem_syscall_procesada;
extern sem_t hay_procesos_ready;

typedef struct {
    int id_cpu;
    int socket_dispatch;
    int socket_interrupt;
    bool esta_libre;
    t_pcb* proceso_en_exec;
} t_cpu_conectada;

extern t_list* lista_cpus_conectadas;

typedef struct {
    t_list* codigo; // Lista de instrucciones
    t_pcb pcb; 
    char* nombre;
    int tamanio;
    estado_proceso_t estado;
} t_proceso_kernel;

typedef struct
{
    char* estado;
    bool activo;
}t_planificador_largo_plazo;

extern t_planificador_largo_plazo* plani_lp;

// Estructura para dispositivos IO conectados
typedef struct {
    char* nombre;
    bool esta_libre;
    int proceso_ejecutando; // PID del proceso ejecutando, -1 si libre
    t_list* cola_bloqueados; // Procesos esperando este dispositivo
    bool conectado;
} t_dispositivo_io;

extern t_list* dispositivos_io_conectados;

// Estructura para timers de suspensión por proceso
typedef struct {
    int pid;
    time_t tiempo_bloqueo;
    pthread_t hilo_timer;
    bool activo;
} t_timer_suspension;

extern t_list* timers_suspension;

// Funciones de inicialización
void inicializar_kernel();
void inicializar_logs();
void inicializar_colas();
void destruir_colas();
void inicializar_sincronizacion();
void inicializar_semaforos();
t_config* inicializar_configs();
void imprimir_configs();

int conectar_a_memoria();
void* atender_cpu_dispatch();
void* atender_cpu_interrupt();
void* iniciar_consola_funciones(void* arg);

// Funciones de comunicación
t_paquete* crear_paquete(int codigo, t_log* logger);
void agregar_a_paquete(t_paquete* paquete, void* valor, int tamanio);
void enviar_paquete(t_paquete* paquete, int socket);
void eliminar_paquete(t_paquete* paquete);
int recibir_operacion(t_log* logger, int socket);
t_list* recibir_paquete_desde_buffer(t_log* logger, int socket);
void agregar_pcb_a_paquete(t_paquete* paquete, t_pcb* pcb);

// Funciones de logging
void iniciar_logger(t_log* logger);

// Funciones de PCB y procesos
t_pcb crear_PCB();
t_proceso_kernel* crear_proceso(char* nombre, int tamanio);
t_cpu_conectada* buscar_cpu_libre();
t_pcb* sacar_de_ready();

// Funciones de interrupciones
void enviar_interrupt(int socket, int pid);

bool procesar_syscall(char* syscall);
bool procesar_io_syscall(char* syscall);
bool procesar_init_proc_syscall(char* syscall);
bool procesar_dump_memory_syscall(char* syscall);
bool procesar_exit_syscall(char* syscall);

// Funciones para manejo de dispositivos IO
t_dispositivo_io* buscar_dispositivo_io(char* nombre);
void agregar_dispositivo_io(char* nombre);
void remover_dispositivo_io(char* nombre);
bool solicitar_io(int pid, char* dispositivo, int tiempo_ms);
void procesar_fin_io(char* dispositivo);
void procesar_desconexion_io(char* dispositivo);

// Funciones auxiliares para syscalls
void bloquear_proceso_por_io(t_pcb* pcb, char* dispositivo);
void desbloquear_proceso_de_io(int pid);
bool comunicar_con_memoria_dump(int pid);
void finalizar_proceso_actual();
int obtener_pid_proceso_actual();

// Funciones para estimación SJF
void actualizar_estimacion_sjf(t_pcb* pcb, int tiempo_real_ejecutado);
int calcular_nueva_estimacion(int estimacion_anterior, int tiempo_real, float alfa);
void procesar_devolucion_cpu(t_pcb* pcb, int tiempo_ejecutado, char* motivo);

// Funciones para manejo de cola READY con SRT
void agregar_proceso_a_ready(t_proceso_kernel* proceso);
void evaluar_desalojo_srt(t_proceso_kernel* proceso_nuevo);

// Funciones para replanificación automática
void replanificar_tras_devolucion(char* motivo);
void optimizar_asignacion_cpus();

// Funciones del Planificador de Mediano Plazo
void iniciar_timer_suspension(int pid);
void cancelar_timer_suspension(int pid);
void* timer_suspension_proceso(void* arg);
void suspender_proceso_automatico(int pid);
bool comunicar_swap_out_memoria(t_proceso_kernel* proceso);
bool comunicar_swap_in_memoria(t_proceso_kernel* proceso);
void verificar_memoria_tras_suspension();
void procesar_fin_io_suspendido(int pid);
void transicion_susp_blocked_a_susp_ready(int pid);
void priorizar_susp_ready_sobre_new();
bool hay_procesos_susp_ready();
void activar_procesos_susp_ready();

// Funciones de logging obligatorio
void log_syscall_recibida(int pid, char* nombre_syscall);
void log_creacion_proceso(int pid);
void log_cambio_estado(int pid, char* estado_anterior, char* estado_actual);
void log_motivo_bloqueo(int pid, char* dispositivo_io);
void log_fin_io(int pid);
void log_desalojo_sjf_srt(int pid);
void log_fin_proceso(int pid);
void log_metricas_estado(int pid);

// Función auxiliar para obtener PID del proceso actual
int obtener_pid_proceso_actual();

// Función auxiliar para buscar proceso por PID
t_proceso_kernel* buscar_proceso_por_pid(int pid);

#endif // KERNEL_MAIN_H
