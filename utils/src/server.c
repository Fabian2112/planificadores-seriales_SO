
#include "server.h"

t_log* logger = NULL;


int iniciar_servidor(t_log* logger, int puerto)
{
    log_info(logger, "Se inicia servidor.");

    int socket_servidor;
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char puerto_char[6];
	sprintf(puerto_char, "%d", puerto); // convierte int a string

    int status = getaddrinfo(NULL, puerto_char, &hints, &servinfo);
    if (status != 0) {
    log_error(logger, "Error en getaddrinfo: %s", gai_strerror(status));
    return -1;
    }

    socket_servidor = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (socket_servidor == -1) {
        log_error(logger, "Error al crear el socket de servidor.");
        freeaddrinfo(servinfo);
        return -1;
    }

    log_info(logger, "Se crea al socket %d del servidor en puerto %d.", socket_servidor, puerto);

    if (bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        log_error(logger, "Error al asociar el socket con el puerto.");
        close(socket_servidor);
        freeaddrinfo(servinfo);
        return -1;
    }

    log_info(logger, "Bind exitoso en puerto %d", puerto); 

    if (listen(socket_servidor, SOMAXCONN) == -1) {
        log_error(logger, "Error al escuchar conexiones entrantes. Errno: %d (%s)", errno, strerror(errno));
        close(socket_servidor);
        freeaddrinfo(servinfo);
        return -1;
    }

    log_info(logger, "Escuchando conexiones entrantes en el puerto %d", puerto);

    if (servinfo != NULL) {
        freeaddrinfo(servinfo);
    }
    log_info(logger, "Listo para escuchar a mi cliente");

    return socket_servidor;
}


int esperar_cliente(t_log* logger, int socket_servidor)
{

    log_info(logger, "inicio funcion esperar_cliente Esperando cliente...");

    if (socket_servidor < 0) {
        log_error(logger, "Socket servidor inválido: %d", socket_servidor);
        return -1;
    }

    int socket_cliente = accept(socket_servidor, NULL, NULL);
    if (socket_cliente == -1) {
        log_error(logger, "Error al aceptar la conexión del cliente.");
    } else {
        log_info(logger, "Cliente conectado.");
    }

    return socket_cliente;
}


int recibir_operacion(t_log* logger, int socket_cliente) {
    if (socket_cliente <= 0) {
        if (logger) log_error(logger, "Error: socket inválido: %d", socket_cliente);
        return -1;
    }
    
    int cod_op;
    ssize_t bytes_recibidos = recv(socket_cliente, &cod_op, sizeof(int), MSG_WAITALL);
    
    if (bytes_recibidos == 0) {
        if (logger) log_info(logger, "El cliente cerró la conexión");
        return -1;
    }
    
    if (bytes_recibidos != sizeof(int)) {
        if (logger) log_error(logger, "Error al recibir código de operación. Recibidos: %zd, esperados: %zu", bytes_recibidos, sizeof(int));
        return -1;
    }
    
    if (logger) log_debug(logger, "Código de operación recibido: %d", cod_op);
    return cod_op;
}

void* recibir_buffer(int* size, int socket_cliente) {   
    if (size == NULL || socket_cliente < 0) {
        log_error(logger, "Parámetros inválidos en recibir_buffer");
        return NULL;
    }

    log_info(logger, "Comenzando la recepción del buffer...");

    // Recibir tamaño
    ssize_t bytes_recibidos = recv(socket_cliente, size, sizeof(int), MSG_WAITALL);
    if (bytes_recibidos <= 0) {
        log_error(logger, "Error al recibir tamaño del buffer. Bytes: %zd, Error: %s", 
                 bytes_recibidos, strerror(errno));
        return NULL;
    } else if (bytes_recibidos != sizeof(int)) {
        log_error(logger, "Tamaño recibido incompleto. Esperados: %zu, Recibidos: %zd",
                 sizeof(int), bytes_recibidos);
        return NULL;
    }

    log_info(logger, "Tamaño de buffer recibido: %d bytes", *size);

    if (*size <= 0) { 
        log_error(logger, "Tamaño de buffer inválido: %d", *size);
        return NULL;
    }

    // Validar tamaño máximo razonable (por ejemplo, 10MB)
    if (*size > 10 * 1024 * 1024) {
        log_error(logger, "Tamaño de buffer excesivamente grande: %d bytes", *size);
        return NULL;
    }

    // Asignar memoria con verificación adicional
    void* buffer = malloc(*size);
    if (buffer == NULL) {
        log_error(logger, "Error al asignar %d bytes para buffer", *size);
        return NULL;
    }

    // Recibir datos con verificación completa
    bytes_recibidos = recv(socket_cliente, buffer, *size, MSG_WAITALL);
    if (bytes_recibidos != *size) {
        log_error(logger, "Error al recibir buffer. Esperados: %d, Recibidos: %zd", *size, bytes_recibidos);
        free(buffer);
        return NULL;
    }

    log_info(logger, "Buffer recibido correctamente (%d bytes)", *size);
    return buffer;
}

char* recibir_mensaje(t_log* logger, int socket_cliente)
{
    log_info(logger, "Comenzando la recepción del mensaje...");

    int size;
    char* buffer;

    if (recv(socket_cliente, &size, sizeof(int), MSG_WAITALL) <= 0 || size <= 0) {
        log_error(logger, "Error al recibir el tamaño del mensaje o tamaño inválido.");
        return NULL;
    }

    log_info(logger, "Tamaño del mensaje recibido: %d bytes", size);

    buffer = malloc(size + 1); // +1 para '\0'
    if (recv(socket_cliente, buffer, size, MSG_WAITALL) <= 0) {
        log_error(logger, "Error al recibir el mensaje.");
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0'; // Aseguramos que termine en '\0'

    log_info(logger, "Mensaje recibido: %s", buffer);

    return buffer;  // Lo devolvemos para ser utilizado
}


// Función específica para recibir paquetes de t_paquete (con código de operación ya leído)
t_list* recibir_paquete_desde_buffer(t_log* logger, int socket_cliente) {
    if (socket_cliente <= 0) {
        if (logger) log_error(logger, "Error: socket inválido: %d", socket_cliente);
        return NULL;
    }
    
    int buffer_size;
    
    // Recibir el tamaño del buffer
    ssize_t bytes_recibidos = recv(socket_cliente, &buffer_size, sizeof(int), MSG_WAITALL);
    if (bytes_recibidos != sizeof(int)) {
        if (logger) log_error(logger, "Error al recibir tamaño del buffer. Recibidos: %zd, esperados: %zu", bytes_recibidos, sizeof(int));
        return NULL;
    }
    
    if (logger) log_info(logger, "Tamaño de buffer recibido: %d bytes", buffer_size);
    
    // Validar que el tamaño sea razonable
    if (buffer_size <= 0) {
        if (logger) log_warning(logger, "Paquete vacío recibido (size = %d)", buffer_size);
        return list_create();
    }
    
    // Validar tamaño máximo (10MB)
    if (buffer_size > 10 * 1024 * 1024) {
        if (logger) log_error(logger, "Tamaño de buffer excesivamente grande: %d bytes", buffer_size);
        return NULL;
    }
    
    // Recibir el contenido del buffer
    void* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        if (logger) log_error(logger, "Error: no se pudo allocar memoria para el buffer de %d bytes", buffer_size);
        return NULL;
    }
    
    bytes_recibidos = recv(socket_cliente, buffer, buffer_size, MSG_WAITALL);
    if (bytes_recibidos != buffer_size) {
        if (logger) log_error(logger, "Error al recibir contenido del buffer. Recibidos: %zd, esperados: %d", bytes_recibidos, buffer_size);
        free(buffer);
        return NULL;
    }
    
    if (logger) log_info(logger, "Buffer recibido correctamente (%d bytes)", buffer_size);
    
    // Deserializar el buffer en una lista
    t_list* valores = list_create();
    int desplazamiento = 0;
    
    while (desplazamiento < buffer_size) {
        // Verificar que hay espacio suficiente para el tamaño del elemento
        if (desplazamiento + sizeof(int) > (size_t)buffer_size) {
            if (logger) log_error(logger, "Error: buffer corrupto al leer tamaño del elemento en desplazamiento %d", desplazamiento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        // Leer el tamaño del siguiente elemento
        int tamanio_elemento;
        memcpy(&tamanio_elemento, buffer + desplazamiento, sizeof(int));
        desplazamiento += sizeof(int);
        
        if (logger) log_debug(logger, "Tamaño del elemento: %d", tamanio_elemento);
        
        // Validar tamaño del elemento
        if (tamanio_elemento <= 0 || tamanio_elemento > 1024) { // Máximo 1KB por elemento
            if (logger) log_error(logger, "Error: tamaño de elemento inválido: %d", tamanio_elemento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        if (desplazamiento + tamanio_elemento > buffer_size) {
            if (logger) log_error(logger, "Error: tamaño de elemento excede el buffer. Elemento: %d, Disponible: %d", 
                                 tamanio_elemento, buffer_size - desplazamiento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        // Leer el contenido del elemento
        char* elemento = malloc(tamanio_elemento);
        if (elemento == NULL) {
            if (logger) log_error(logger, "Error: no se pudo allocar memoria para elemento de %d bytes", tamanio_elemento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        memcpy(elemento, buffer + desplazamiento, tamanio_elemento);
        desplazamiento += tamanio_elemento;
        
        // Agregar a la lista
        list_add(valores, elemento);
        
        if (logger) log_debug(logger, "Elemento %d agregado a la lista", list_size(valores));
    }
    
    free(buffer);
    
    if (logger) {
        log_info(logger, "Paquete deserializado exitosamente - %d elementos, tamaño total: %d bytes", 
                list_size(valores), buffer_size);
    }
    
    return valores;
}


t_list* recibir_paquete(t_log* logger, int socket_cliente) {
    if (socket_cliente <= 0) {
        if (logger) log_error(logger, "Error: socket inválido: %d", socket_cliente);
        return NULL;
    }
    
    size_t size;

    // Recibir el tamaño del buffer
    ssize_t bytes_recibidos = recv(socket_cliente, &size, sizeof(size_t), MSG_WAITALL);
    if (bytes_recibidos != sizeof(size_t)) {
        if (logger) log_error(logger, "Error al recibir tamaño del buffer. Recibidos: %zd, esperados: %zu", bytes_recibidos, sizeof(size_t));
        return NULL;
    }
    
    if (logger) log_info(logger, "Tamaño de buffer recibido: %ld bytes", size);
    
    // Validar que el tamaño sea razonable
    if (size <= 0) {
        if (logger) log_warning(logger, "Paquete vacío recibido (size = %ld)", size);
        return list_create();
    }
    
    // Validar tamaño máximo (10MB)
    if (size > 10 * 1024 * 1024) {
        if (logger) log_error(logger, "Tamaño de buffer excesivamente grande: %ld bytes", size);
        return NULL;
    }
    
    // Recibir el contenido del buffer
    void* buffer = malloc(size);
    if (buffer == NULL) {
        if (logger) log_error(logger, "Error: no se pudo allocar memoria para el buffer de %ld bytes", size);
        return NULL;
    }
    
    bytes_recibidos = recv(socket_cliente, buffer, size, MSG_WAITALL);
    if (bytes_recibidos != (ssize_t)size) {
        if (logger) log_error(logger, "Error al recibir contenido del buffer. Recibidos: %zd, esperados: %ld", bytes_recibidos, size);
        free(buffer);
        return NULL;
    }
    
    if (logger) log_info(logger, "Buffer recibido correctamente (%ld bytes)", size);
    
    // Deserializar el buffer en una lista
    t_list* valores = list_create();
    size_t desplazamiento = 0;
    
    while (desplazamiento < size) {
        // Verificar que hay espacio suficiente para el tamaño del elemento
        if (desplazamiento + sizeof(int) > size) {
            if (logger) log_error(logger, "Error: buffer corrupto al leer tamaño del elemento en desplazamiento %ld", desplazamiento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        // Leer el tamaño del siguiente elemento
        int tamanio_elemento;
        memcpy(&tamanio_elemento, buffer + desplazamiento, sizeof(int));
        desplazamiento += sizeof(int);
        
        if (logger) log_debug(logger, "Tamaño del elemento: %d", tamanio_elemento);
        
        // Validar tamaño del elemento
        if (tamanio_elemento <= 0 || tamanio_elemento > 1024) { // Máximo 1KB por elemento
            if (logger) log_error(logger, "Error: tamaño de elemento inválido: %d", tamanio_elemento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        if (desplazamiento + (size_t)tamanio_elemento > size) {
            if (logger) log_error(logger, "Error: tamaño de elemento excede el buffer. Elemento: %d, Disponible: %ld", 
                                 tamanio_elemento, size - desplazamiento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        // Leer el contenido del elemento
        char* elemento = malloc(tamanio_elemento);
        if (elemento == NULL) {
            if (logger) log_error(logger, "Error: no se pudo allocar memoria para elemento de %d bytes", tamanio_elemento);
            list_destroy_and_destroy_elements(valores, free);
            free(buffer);
            return NULL;
        }
        
        memcpy(elemento, buffer + desplazamiento, tamanio_elemento);
        desplazamiento += tamanio_elemento;
        
        // Agregar a la lista
        list_add(valores, elemento);
        
        if (logger) log_debug(logger, "Elemento %d agregado a la lista", list_size(valores));
    }
    
    free(buffer);
    
    if (logger) {
        log_info(logger, "Paquete deserializado exitosamente - %d elementos, tamaño total: %ld bytes", 
                list_size(valores), size);
    }
    
    return valores;
}

void* recibir_contenido_paquete(int* size, int socket_cliente) {
    if (size == NULL || socket_cliente < 0) {
        log_error(logger, "Parámetros inválidos en recibir_contenido_paquete");
        return NULL;
    }

    // 1. Recibir tamaño del contenido
    if(recv(socket_cliente, size, sizeof(int), MSG_WAITALL) != sizeof(int)) {
        log_error(logger, "Error al recibir tamaño del contenido");
        return NULL;
    }

    // Si no hay contenido, retornar NULL
    if(*size <= 0) {
        log_info(logger, "Paquete sin contenido (size: %d)", *size);
        return NULL;
    }

    // Validar tamaño máximo
    if (*size > 10 * 1024 * 1024) {
        log_error(logger, "Tamaño de contenido excesivamente grande: %d bytes", *size);
        return NULL;
    }

    // 2. Recibir el contenido
    void* contenido = malloc(*size);
    if(contenido == NULL) {
        log_error(logger, "Error al asignar memoria para contenido");
        return NULL;
    }

    if(recv(socket_cliente, contenido, *size, MSG_WAITALL) != *size) {
        log_error(logger, "Error al recibir contenido del paquete");
        free(contenido);
        return NULL;
    }

    log_info(logger, "Contenido recibido correctamente (%d bytes)", *size);
    return contenido;
}

/*
// Función para recibir un paquete completo (estructura t_paquete)
t_paquete* recibir_paquete_completo(t_log* logger, int socket_cliente) {
    if (socket_cliente <= 0) {
        if (logger) log_error(logger, "Error: socket inválido: %d", socket_cliente);
        return NULL;
    }
    
    // Crear el paquete
    t_paquete* paquete = malloc(sizeof(t_paquete));
    if (paquete == NULL) {
        if (logger) log_error(logger, "Error: no se pudo allocar memoria para el paquete");
        return NULL;
    }
    
    // El código de operación ya fue leído por recibir_operacion()
    // Solo necesitamos recibir el buffer
    
    // Crear el buffer
    paquete->buffer = malloc(sizeof(t_buffer));
    if (paquete->buffer == NULL) {
        if (logger) log_error(logger, "Error: no se pudo allocar memoria para el buffer");
        free(paquete);
        return NULL;
    }
    
    // Recibir tamaño del buffer
    ssize_t bytes_recibidos = recv(socket_cliente, &(paquete->buffer->size), sizeof(int), MSG_WAITALL);
    if (bytes_recibidos != sizeof(int)) {
        if (logger) log_error(logger, "Error al recibir tamaño del buffer. Recibidos: %zd, esperados: %zu", bytes_recibidos, sizeof(int));
        free(paquete->buffer);
        free(paquete);
        return NULL;
    }
    
    // Validar tamaño
    if (paquete->buffer->size > 10 * 1024 * 1024) {
        if (logger) log_error(logger, "Tamaño de buffer excesivamente grande: %d bytes", paquete->buffer->size);
        free(paquete->buffer);
        free(paquete);
        return NULL;
    }
    
    // Recibir contenido del buffer (si tiene datos)
    if (paquete->buffer->size > 0) {
        paquete->buffer->stream = malloc(paquete->buffer->size);
        if (paquete->buffer->stream == NULL) {
            if (logger) log_error(logger, "Error: no se pudo allocar memoria para el stream");
            free(paquete->buffer);
            free(paquete);
            return NULL;
        }
        
        bytes_recibidos = recv(socket_cliente, paquete->buffer->stream, paquete->buffer->size, MSG_WAITALL);
        if (bytes_recibidos != paquete->buffer->size) {
            if (logger) log_error(logger, "Error al recibir contenido del buffer. Recibidos: %zd, esperados: %d", bytes_recibidos, paquete->buffer->size);
            free(paquete->buffer->stream);
            free(paquete->buffer);
            free(paquete);
            return NULL;
        }
    } else {
        paquete->buffer->stream = NULL;
    }
    
    if (logger) {
        log_info(logger, "Paquete completo recibido exitosamente - Tamaño: %d bytes", paquete->buffer->size);
    }
    
    return paquete;
}
*/

bool limpiar_buffer(int socket_fd) {
    if (socket_fd <= 0) {
        log_error(logger, "Descriptor de socket inválido en limpiar_buffer: %d", socket_fd);
        return false;
    }

    char buffer_temp[1024];
    int bytes_leidos;
    bool buffer_limpio = false;
    int intentos = 0;
    const int max_intentos = 10; // Para evitar bucles infinitos

    // Configurar el socket como no bloqueante temporalmente
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        log_error(logger, "Error al obtener flags del socket: %s", strerror(errno));
        return false;
    }
    
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error(logger, "Error al configurar socket como no bloqueante: %s", strerror(errno));
        return false;
    }

    // Leer todo lo que haya en el buffer
    while (intentos < max_intentos && !buffer_limpio) {
        bytes_leidos = recv(socket_fd, buffer_temp, sizeof(buffer_temp), MSG_DONTWAIT);
        
        if (bytes_leidos > 0) {
            log_warning(logger, "Descartados %d bytes residuales del buffer del socket %d", 
                       bytes_leidos, socket_fd);
            intentos++;
            continue;
        } else if (bytes_leidos == 0) {
            // Conexión cerrada por el otro extremo
            log_info(logger, "Conexión cerrada mientras se limpiaba el buffer");
            buffer_limpio = true;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No hay más datos para leer
                buffer_limpio = true;
            } else {
                // Error real
                log_error(logger, "Error al limpiar buffer del socket %d: %s", 
                         socket_fd, strerror(errno));
                break;
            }
        }
    }

    // Restaurar flags originales del socket
    if (fcntl(socket_fd, F_SETFL, flags) == -1) {
        log_error(logger, "Error al restaurar flags del socket: %s", strerror(errno));
    }

    if (intentos >= max_intentos) {
        log_warning(logger, "Se alcanzó el máximo de intentos al limpiar el buffer del socket %d", socket_fd);
    }

    return buffer_limpio;
}


int recibir_paquete_completo(t_log* logger, int socket, void** buffer) {
    uint32_t header[2]; // cod_op y tamaño
    
    // 1. Recibir encabezado fijo (8 bytes)
    int recibido = recv(socket, header, sizeof(header), MSG_WAITALL);
    if(recibido != sizeof(header)) {
        if(recibido == 0) return -1; // Conexión cerrada
        return 0; // Error
    }
    
    uint32_t cod_op = ntohl(header[0]);
    uint32_t tamanio = ntohl(header[1]);
    
    if(logger) {
        log_info(logger, "Recibido cod_op: %d, tamaño: %d", cod_op, tamanio);
    }

    // 2. Reservar y recibir payload
    *buffer = malloc(tamanio);
    recibido = recv(socket, *buffer, tamanio, MSG_WAITALL);
    if(recibido != (ssize_t)tamanio) {
        free(*buffer);
        return 0;
    }
    
    return cod_op;
}