#ifndef CPU_MAIN_H
#define CPU_MAIN_H

#include <cliente.h>
#include <conexiones.h>

// Logger y Config
extern t_log* cpu_logger;
extern t_config* cpu_config;

// Strings
extern char* IP_MEMORIA;
extern char* IP_KERNEL;
extern char* REEMPLAZO_TLB;
extern char* REEMPLAZO_CACHE;
extern char* LOG_LEVEL;

// Enteros
extern int PUERTO_MEMORIA;
extern int PUERTO_KERNEL_DISPATCH;
extern int PUERTO_KERNEL_INTERRUPT;
extern int ENTRADAS_TLB;
extern int ENTRADAS_CACHE;
extern int RETARDO_CACHE;

extern int fd_memoria;
extern int fd_kernel_dispatch;
extern int fd_kernel_interrupt;
extern int fd_cpu_interrupt;


// Funciones de inicialización
void inicializar_cpu(void);
void inicializar_kernel();
void inicializar_logs();
void inicializar_sincronizacion();
void inicializar_semaforos();
t_config* inicializar_configs(void);
void imprimir_configs();

// Funciones principales de CPU
void iniciar_cpu(int conexion_memoria, int conexion_dispatch, int conexion_interrupt);
void* conectar_kernel_dispatch(void* arg);
void* conectar_kernel_interrupt(void* arg);
void* conectar_cpu_memoria(void* arg);

// Funciones de manejo de instrucciones
void recibir_instruccion(int fd_memoria);
void procesar_instruccion(char* instruccion);
bool es_syscall(char* instruccion);
t_instruccion decode_instruction(char* instruction_string);
void free_instruction(t_instruccion* instruction);
char* fetch_instruction(t_pcb* pcb, int conexion_memoria);
ExecutionResult execute_instruction(t_instruccion* instruction, t_pcb* pcb, int conexion_memoria);
ExecutionResult ciclo_instruccion(t_pcb* pcb, int conexion_memoria, int conexion_interrupt);

// Funciones de comunicación
bool recibir_pcb(int socket, t_pcb* pcb);
bool recibir_datos(int socket, void* buffer, size_t tamanio);
bool check_interrupt(int conexion_interrupt, int pid);

// Funciones de memoria
void* leer_de_memoria_simple(int* direccion_fisica, size_t tamanio, int conexion_memoria, int pid);
int escribir_en_memoria_simple(int* direccion_fisica, void* datos, size_t tamanio, int conexion_memoria, int pid);

// Funciones MMU (para resolver dependencias)
void iniciar_MMU(void);
void finalizar_MMU(void);
void limpiar_tlb_proceso(int pid);
int* obtener_direccion_fisica(int dir, int pid, int conexion_memoria);
int* obtener_direccion_fisica_con_cache(int dir, int pid, int conexion_memoria);

// Funciones de caché (para resolver dependencias)
void inicializar_cache(int entradas, char* algoritmo);
bool cache_habilitada(void);
void desalojar_proceso_cache(int pid, int conexion_memoria);
bool escribir_en_cache(int pagina, int pid, int offset, void* datos, size_t tamanio);
bool leer_de_cache(int pagina, int pid, int offset, void* buffer, size_t tamanio);


#endif // CPU_MAIN_H