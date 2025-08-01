#ifndef IO_H
#define IO_H

#include <server.h>

extern t_config* io_config;
extern t_log* io_logger;

// Variables de configuración
extern int IP_KERNEL;
extern int PUERTO_KERNEL;
extern char* NOMBRE_IO;
extern int TIEMPO_IO;
extern char* LOG_LEVEL;
extern int fd_kernel;

// Estructura para dispositivo I/O
typedef struct {
    char* nombre;
    int tiempo_operacion;
    bool conectado;
    int socket_kernel; // Socket para comunicación con Kernel
} t_dispositivo_io;

typedef enum {
    IO_SOLICITUD = 1,
    IO_FINALIZACION = 2,
    IO_DESCONEXION = 3
} t_tipo_mensaje_io;

typedef struct {
    t_tipo_mensaje_io tipo;
    int pid;
} t_mensaje_io;

// Funciones principales
void inicializar_io();
int iniciar_servidor_io();
void conectar_con_kernel(void);
void enviar_handshake(char* nombre_io);
bool realizar_handshake_kernel(int socket_kernel);
void conectar_con_kernel();
void atender_operaciones_io();

char* convertir_ip_a_string(int ip_int);
void limpiar_buffer_socket(int socket_fd);
void finalizar_io();

#endif
