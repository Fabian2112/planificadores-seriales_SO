#ifndef SERVER_H_
#define SERVER_H_

#include "conexion.h"


//SERVIDOR

int iniciar_servidor(t_log* logger, int puerto);
int esperar_cliente(t_log* logger, int socket_servidor);
int recibir_operacion(t_log* logger, int socket_cliente);
void* recibir_buffer(int* size, int socket_cliente);
char* recibir_mensaje(t_log* logger, int socket_cliente);
t_list* recibir_paquete(t_log* logger, int socket_cliente);
void* recibir_contenido_paquete(int* size, int socket_cliente);
t_list* recibir_paquete_desde_buffer(t_log* logger, int socket_cliente);
bool limpiar_buffer(int socket_fd);
int recibir_paquete_completo(t_log* logger, int socket, void** buffer);

//void eliminar_paquete(t_paquete* paquete);



#endif /* SERVER_H_ */
