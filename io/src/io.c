#include "io.h"
#include <conexiones.h>
#include <server.h>
#include <cliente.h>

t_config* io_config = NULL;
t_log* io_logger = NULL;
int IP_KERNEL = 0;
int PUERTO_KERNEL = 0;
char* NOMBRE_IO = NULL;
int TIEMPO_IO = 0;
char* LOG_LEVEL = NULL;
int fd_kernel = -1;
t_dispositivo_io* dispositivo_io = NULL;

// Función para convertir int IP a string
char* convertir_ip_a_string(int ip_int) {
    static char ip_str[16]; // Buffer estático para la IP
    
    // Si el int representa una IP en formato de entero único (como 2130706433 para 127.0.0.1)
    unsigned char bytes[4];
    bytes[0] = ip_int & 0xFF;
    bytes[1] = (ip_int >> 8) & 0xFF;
    bytes[2] = (ip_int >> 16) & 0xFF;
    bytes[3] = (ip_int >> 24) & 0xFF;
    
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", bytes[3], bytes[2], bytes[1], bytes[0]);
    
    // Si el formato es diferente (como solo "127" representando "127.0.0.1")
    // Puedes usar esta lógica alternativa:
    if (ip_int == 127) {
        strcpy(ip_str, "127.0.0.1");
    } else if (ip_int == 0) {
        strcpy(ip_str, "0.0.0.0");
    }
    // Agregar más casos según necesites
    
    return ip_str;
}

// Función auxiliar para construir ruta del config
char* obtener_ruta_config() {
    char* path_base = "../io.config";
    char* path_absoluto = realpath(path_base, NULL);
    
    if (path_absoluto == NULL) {
        printf("Error: No se pudo encontrar el archivo de configuración en %s\n", path_base);
        exit(EXIT_FAILURE);
    }
    
    return path_absoluto;
}

void inicializar_io() {
    // Obtener ruta del archivo de configuración
    char* config_path = obtener_ruta_config();

    // Inicializar logger
    io_logger = log_create("io.log", "IO_LOG", 1, LOG_LEVEL_INFO);
    if (io_logger == NULL) {
        perror("Error al crear logger");
        exit(EXIT_FAILURE);
    }

    // Cargar configuración
    io_config = config_create(config_path);
    free(config_path);
    if (!io_config) {
        log_error(io_logger, "No se pudo cargar el archivo de configuración");
        exit(EXIT_FAILURE);
    }

    // Validar campos obligatorios
    if (!config_has_property(io_config, "NOMBRE_IO") || 
        !config_has_property(io_config, "TIEMPO_IO") ||
        !config_has_property(io_config, "IP_KERNEL") ||
        !config_has_property(io_config, "PUERTO_KERNEL")) {
        log_error(io_logger, "Configuración incompleta en io.config");
        exit(EXIT_FAILURE);
    }

    // Cargar configuraciones
    IP_KERNEL = config_get_int_value(io_config, "IP_KERNEL");
    PUERTO_KERNEL = config_get_int_value(io_config, "PUERTO_KERNEL");
    NOMBRE_IO = config_get_string_value(io_config, "NOMBRE_IO");
    TIEMPO_IO = config_get_int_value(io_config, "TIEMPO_IO");
    LOG_LEVEL = config_get_string_value(io_config, "LOG_LEVEL");

    log_info(io_logger, "IO iniciado correctamente\n");

    // Inicializar estructura del dispositivo
    dispositivo_io = malloc(sizeof(t_dispositivo_io));
    dispositivo_io->nombre = strdup(config_get_string_value(io_config, "NOMBRE_IO"));
    dispositivo_io->tiempo_operacion = config_get_int_value(io_config, "TIEMPO_IO");
    dispositivo_io->conectado = false;
    dispositivo_io->socket_kernel = -1;

    log_info(io_logger, "=== Configuración del Dispositivo I/O ===");
    log_info(io_logger, "Nombre: %s", dispositivo_io->nombre);
    log_info(io_logger, "Tiempo de operación: %d ms", dispositivo_io->tiempo_operacion);
    log_info(io_logger, "Conectando a Kernel en %s:%d", 
             config_get_string_value(io_config, "IP_KERNEL"),
             config_get_int_value(io_config, "PUERTO_KERNEL"));
}


void conectar_con_kernel() {
    char* ip_string = convertir_ip_a_string(IP_KERNEL);
    
    log_info(io_logger, "Conectando a Kernel en %s:%d", ip_string, PUERTO_KERNEL);
    
    fd_kernel = crear_conexion(io_logger, ip_string, PUERTO_KERNEL);
    if (fd_kernel == -1) {
        log_error(io_logger, "No se pudo conectar al Kernel");
        exit(EXIT_FAILURE);
    }

    // Realizar handshake
    if (!realizar_handshake_kernel(fd_kernel)) {
        log_error(io_logger, "Handshake con Kernel falló");
        close(fd_kernel);
        exit(EXIT_FAILURE);
    }

    log_info(io_logger, "Conexión y handshake con Kernel exitosos");
}

// Handshake simplificado
bool realizar_handshake_kernel(int socket_kernel) {
    // Recibir identificación del kernel
    int identificador;
    ssize_t recibido = recv(socket_kernel, &identificador, sizeof(int), MSG_WAITALL);
    
    if (recibido != sizeof(int)) {
        log_error(io_logger, "Error al recibir identificación del kernel");
        return false;
    }
    
    log_info(io_logger, "Identificación recibida: %d", identificador);
    
    if (identificador != KERNEL) {
        log_error(io_logger, "Identificación inválida: %d", identificador);
        return false;
    }

    // Enviar confirmación
    bool confirmacion = true;
    ssize_t enviado = send(socket_kernel, &confirmacion, sizeof(bool), 0);
    
    if (enviado != sizeof(bool)) {
        log_error(io_logger, "Error al enviar confirmación");
        return false;
    }

    log_info(io_logger, "Handshake completado exitosamente");

    dispositivo_io->conectado = true;
    dispositivo_io->socket_kernel = socket_kernel;
    return true;
}



void atender_operaciones_io() {
    log_info(io_logger, "Dispositivo %s esperando solicitudes del Kernel...", dispositivo_io->nombre);
    
    while (dispositivo_io->conectado) {
        t_mensaje_io mensaje;
        
        // Recibir mensaje del Kernel de forma bloqueante
        ssize_t recibido = recv(dispositivo_io->socket_kernel, &mensaje, sizeof(t_mensaje_io), MSG_WAITALL);
        
        // Verificar estado de la conexión
        if (recibido <= 0) {
            if (recibido == 0) {
                log_info(io_logger, "Kernel cerró la conexión");
            } else {
                log_error(io_logger, "Error en recv: %s", strerror(errno));
            }
            dispositivo_io->conectado = false;
            break;
        }
        
        // Verificar que recibimos el mensaje completo
        if (recibido != sizeof(t_mensaje_io)) {
            log_error(io_logger, "Mensaje incompleto: recibido %ld bytes, esperado %ld", 
                     recibido, sizeof(t_mensaje_io));
            continue;
        }
        
        // Procesar mensaje según su tipo
        switch (mensaje.tipo) {
            case IO_SOLICITUD:
                log_info(io_logger, "Ejecutando I/O - PID %d en %s (%d ms)", 
                        mensaje.pid, dispositivo_io->nombre, dispositivo_io->tiempo_operacion);
                
                // Realizar operación I/O
                usleep(dispositivo_io->tiempo_operacion * 1000);
                
                log_info(io_logger, "I/O completada - PID %d", mensaje.pid);
                
                // Enviar confirmación al Kernel
                t_mensaje_io respuesta = {IO_FINALIZACION, mensaje.pid};
                ssize_t enviado = send(dispositivo_io->socket_kernel, &respuesta, sizeof(t_mensaje_io), 0);
                
                if (enviado <= 0) {
                    log_error(io_logger, "Error al enviar respuesta: %s", strerror(errno));
                    dispositivo_io->conectado = false;
                }
                break;
                
            case IO_DESCONEXION:
                log_info(io_logger, "Desconexión solicitada por el Kernel");
                dispositivo_io->conectado = false;
                break;
                
            default:
                log_warning(io_logger, "Mensaje desconocido tipo %d", mensaje.tipo);
                break;
        }
    }
    
    log_info(io_logger, "Finalizando atención de operaciones I/O");
}


void finalizar_io() {
    log_info(io_logger, "Finalizando módulo I/O...");
    
    // Liberar recursos del dispositivo
    if (dispositivo_io) {
        free(dispositivo_io->nombre);
        free(dispositivo_io);
    }
    
    // Liberar configuración
    if (io_config) {
        config_destroy(io_config);
    }
    
    // Cerrar logger
    if (io_logger) {
        log_destroy(io_logger);
    }
    
    log_info(io_logger, "Módulo I/O finalizado correctamente");
}


void limpiar_buffer_socket(int socket_fd) {
    char buffer[1024];
    ssize_t bytes_leidos;
    
    // Configurar socket como no bloqueante temporalmente
    int flags_originales = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags_originales | O_NONBLOCK);
    
    // Leer todos los datos disponibles
    do {
        bytes_leidos = recv(socket_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (bytes_leidos > 0) {
            log_warning(io_logger, "Limpiando %ld bytes residuales", bytes_leidos);
        }
    } while (bytes_leidos > 0);
    
    // Restaurar flags originales
    fcntl(socket_fd, F_SETFL, flags_originales);
}