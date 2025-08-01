#ifndef CONEXION_H_
#define CONEXION_H_

#include <stdio.h>          // printf, fprintf, perror
#include <stdlib.h>         // malloc, free
#include <fcntl.h>        // fcntl, O_NONBLOCK

#include <commons/log.h>  // Necesario para t_log*
#include <commons/collections/dictionary.h>
#include <commons/collections/list.h>
#include <commons/config.h>
#include <commons/string.h>
#include <commons/temporal.h>
#include <commons/collections/queue.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <stdint.h>

#include <readline/readline.h>
#include <signal.h>
#include <unistd.h>         // close, sleep
#include <sys/mman.h>
#include <sys/types.h>	 	// ssize_t
#include <sys/socket.h>     // socket, bind, listen, connect, setsockopt
#include <sys/time.h>         // gettimeofday
#include <netdb.h>          // struct addrinfo, getaddrinfo
#include <string.h>         // memset, strerror
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>        // pthread_create, pthread_join
#include <libgen.h>  // Para dirname()
#include <math.h>
#include <time.h>
#include <stddef.h>

#include <errno.h>          // errno

// Constantes que podrían usar otros módulos
#define OK 0
#define ERROR -1

// Variables compartidas
extern char* instruccion_global;
extern bool instruccion_disponible;
extern bool conexion_activa;
extern bool syscall_en_proceso;
extern t_log* logger;
extern t_dictionary* procesos_activos;  // Diccionario de procesos activos (key: PID, value: t_proceso*)
extern t_dictionary* procesos_suspendidos; // Diccionario de procesos suspendidos
extern void* espacio_usuario;           // Espacio contiguo de memoria
extern t_list* tablas_paginas;          // Lista de tablas de páginas
extern t_list* operaciones_io_activas;
extern FILE* swap_file;                 // Archivo de swap
extern bool* marcos_libres;             // Array para control de marcos libres
extern int total_marcos;                // Total de marcos disponibles
extern char* memoria_fisica;
extern t_list* marcos_en_uso;
extern int puntero_clock;
extern int cantidad_entradas;
extern char* algoritmo_reemplazo;      // Algoritmo de reemplazo de páginas
extern t_list* tlb;                     // Tabla de Localidad de Referencia (TLB)

// Mecanismos de sincronización
extern pthread_mutex_t mutex_instruccion;
extern pthread_cond_t cond_instruccion;
extern pthread_mutex_t mutex_archivo;
extern pthread_mutex_t mutex_path;
extern pthread_cond_t cond_archivo;
extern pthread_mutex_t mutex_envio_cpu;
extern pthread_mutex_t mutex_comunicacion_memoria;
extern pthread_mutex_t mutex_comunicacion_cpu;
extern pthread_mutex_t mutex_syscall;
extern pthread_mutex_t mutex_syscall_kernel;
extern pthread_mutex_t mutex_instruccion_pendiente;
extern pthread_mutex_t mutex_archivo_rewind;
extern pthread_mutex_t mutex_archivo_instrucciones;
extern pthread_mutex_t mutex_archivo_solicitud_instruccion; // Mutex para solicitud de instrucción
extern pthread_mutex_t mutex_archivo_estado; // Mutex para acceso al estado del archivo
extern pthread_mutex_t mutex_archivo_listo_estado; // Mutex para verificar estado del archivo
extern pthread_mutex_t mutex_archivo_cierre;
extern pthread_mutex_t mutex_swap;      // Mutex para operaciones de swap
extern pthread_mutex_t mutex_marcos;    // Mutex para control de marcos libres
extern pthread_mutex_t mutex_procesos; // Mutex para acceso a procesos activos y suspendidos
extern pthread_mutex_t mutex_io_activas; 

// Semáforos para sincronización entre módulos
extern sem_t sem_kernel_memoria_hs;       // Kernel espera handshake de memoria
extern sem_t sem_kernel_cpu_hs;           // Kernel espera handshake de CPU
extern sem_t sem_memoria_kernel_ready;    // Memoria lista para recibir archivo
extern sem_t sem_memoria_cpu_hs;          // Memoria espera handshake de CPU
extern sem_t sem_cpu_memoria_hs;          // CPU espera handshake de memoria
extern sem_t sem_cpu_kernel_hs;           // CPU espera handshake de kernel
extern sem_t sem_archivo_listo;           // Archivo listo para ser procesado
extern sem_t sem_archivo_listo_solicitado; // Semáforo para indicar que el archivo está listo
extern sem_t sem_instruccion_lista;       // Instrucción lista para ser enviada
extern sem_t sem_servidor_cpu_listo;     // Servidor CPU listo para recibir conexiones
extern sem_t sem_instruccion_disponible; // Instrucción disponible para ser procesada
extern sem_t sem_instruccion_cpu_kernel;
extern sem_t sem_modulos_conectados;      // Módulos conectados y listos para operar
extern sem_t sem_instruccion_procesada; // Sincronización para indicar que una instrucción ha sido procesada
extern sem_t sem_solicitud_procesada; // Sincronización para indicar que una solicitud ha sido procesada
extern sem_t sem_solicitud_lista; // Sincronización para esperar solicitudes de CPU
extern sem_t sem_respuesta_lista; // Sincronización para esperar respuestas de memoria
extern sem_t sem_procesamiento_libre; // Sincronización para indicar que el procesamiento está libre
extern sem_t sem_syscall_procesada; // Sincronización para indicar que una syscall ha sido procesada
extern sem_t sem_io_completada; // Sincronización para indicar que una operación de I/O ha sido completada
extern sem_t *sem_respuesta_memoria; // Semáforo para indicar que Memoria ha respondido a una solicitud de instrucción

// Códigos de operación para identificación de módulos
typedef enum {
    KERNEL,
    CPU,
    MEMORIA
} modulo_id;

// Estados de proceso para PCB
typedef enum {
    ESTADO_NEW,
    ESTADO_READY,
    ESTADO_EXEC,
    ESTADO_BLOCKED,
    ESTADO_EXIT,
    ESTADO_SUSP_READY,
    ESTADO_SUSP_BLOCKED
} estado_proceso_t;

// Process Control Block - Compartido entre kernel y CPU
typedef struct {
    int pid;
    int pc;
    int tiempo_estimado;
    int tiempo_inicio_rafaga;    // Timestamp del inicio de la ráfaga actual
    estado_proceso_t estado;
    t_queue* metricas_tiempo;    // Cola para almacenar métricas de tiempo
    t_queue* metricas_estado;    // Cola para almacenar cambios de estado
} t_pcb;

typedef struct {
    char* nombre_archivo;
    int tamanio_archivo;
} t_datos_kernel;

typedef struct {
    char instruccion[256];
    bool es_syscall;
    bool procesada;
} t_instruccion_pendiente;

typedef struct {
    int marco;
    bool presente;
    bool modificada;
    bool referenciada;
    void* subtabla;
} t_entrada_pagina;

typedef struct {
    int pid;
    int pagina;
    void* marco;
    time_t timestamp; // para LRU
    int orden_insercion; // para FIFO
} EntradaTLB;

// Estructura para tabla de páginas (simplificada)
typedef struct {
    int nivel;
    int entradas_usadas;
    t_entrada_pagina** entradas; // Array de punteros a siguiente nivel o marcos
} t_tabla_paginas;


typedef struct {
    char* dispositivo;
    int tiempo_ms;
    int pid_proceso;
    pthread_t hilo_io;
    bool completada;
} t_operacion_io;

// Códigos de operación para mensajes
typedef enum
{
    MENSAJE,
    PAQUETE,
    ENVIO_ARCHIVO_KERNEL,
    CREAR_PROCESO,
    FINALIZAR_PROCESO,
    DUMP_MEMORY,
    SUSPENDER_PROCESO,
    REACTIVAR_PROCESO,
    SOLICITUD_ARCHIVO,
    SOLICITUD_ARCHIVO_KERNEL,
    ARCHIVO_LISTO,
    SOLICITUD_INSTRUCCION,
    LECTURA_MEMORIA,
    ESCRITURA_MEMORIA,
    ESPERANDO_ARCHIVO,
    FIN_ARCHIVO,
    INSTRUCCION_PROCESADA,
    INVALID_INSTRUCTION,
    NOOP,
    WRITE,
    READ,
    GOTO,
    IO,
    INIT_PROC,
    EXIT,
    SYSCALL,
    SYSCALL_OK,
    SYSCALL_ERROR,
    INTERRUPCION,
    SOLICITUD_PAGINA,
    PCB,
    PCB_A_EJECUTAR,
    PCB_DEVUELTO,
    OP_INVALID = -1,
    HANDSHAKE_IO,
    HANDSHAKE,
}op_code;

// Eliminando la definición duplicada del enum op_code

typedef enum {
    CONTINUE_EXECUTION,
    INTERRUPT_RECEIVED,
    SYSCALL_IO,
    SYSCALL_INIT_PROC,
    SYSCALL_DUMP_MEMORY,
    PROCESS_EXIT
} ExecutionResult;



typedef struct
{
    int size;
    void* stream;
} t_buffer;


// Estructura para métricas por proceso
typedef struct {
    int accesos_tablas_paginas;
    int instrucciones_solicitadas;
    int bajadas_swap;
    int subidas_memoria;
    int lecturas_memoria;
    int escrituras_memoria;
} t_metricas_proceso;

// Estructura para proceso
typedef struct {
    int pid;
    int tamanio;
    FILE* archivo_pseudocodigo;
    t_list* instrucciones;
    t_list* paginas; // Lista de páginas asignadas
    t_list* paginas_swap; // Lista de t_pagina_swap para páginas en swap
    t_metricas_proceso metricas;
    t_tabla_paginas* tabla_paginas; // Estructura de tablas de páginas multinivel
} t_proceso;


typedef struct {
    int pagina;             // Número de página lógica
    int marco;              // Marco físico correspondiente
    int pid;                // PID del proceso propietario
    bool usado;             // Bit de uso (para algoritmos CLOCK)
    bool modificado;        // Bit de modificación (dirty bit)
    bool ocupado;           // Si la entrada está ocupada
    void* contenido;        // Contenido de la página (4KB)
} entrada_cache_t;

// Estructura para información de páginas en swap
typedef struct {
    int pid;
    int pagina;
    int posicion_swap; // Posición en el archivo swap
} t_pagina_swap;

// Estructura para intercambiar instrucciones
typedef struct {
    op_code tipo;
    uint32_t direccion;
    char* datos;
    uint32_t tamanio;
    uint32_t valor;
    char* dispositivo;
    uint32_t tiempo;
    char* archivo;
} t_instruccion;


typedef struct
{
    op_code codigo_operacion;
    t_buffer* buffer;
} t_paquete;


void saludar(char* quien);
void eliminar_paquete(t_paquete* paquete);
void iniciar_logger(t_log* logger);
void print_lista(void* valor);
void inicializar_sincronizacion();
void destruir_sincronizacion();
void inicializar_semaforos();
void destruir_semaforos();
void limpiar_buffer_comunicacion(int fd);
bool limpiar_buffer_completo(int socket_fd);
bool limpiar_buffer_antes_operacion(int socket_fd, const char* operacion);

#endif // CONEXION_H_