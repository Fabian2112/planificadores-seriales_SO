#include <protocolo.h>
#include <server.h>


int* recibir_int(t_log* logger, void* coso)
{
	int* buffer_int = malloc(sizeof(int));
	memcpy(buffer_int,coso,sizeof(int));
	log_info(logger, "Me llego el numero: %d", *buffer_int);
	return buffer_int;
}

void crear_buffer(t_paquete* paquete)
{
	paquete->buffer = malloc(sizeof(t_buffer));
	paquete->buffer->size = 0;
	paquete->buffer->stream = NULL;
}


t_list* recibir_paquete_int(int socket_cliente)
{
	int size;
	int desplazamiento = 0;
	t_list* valores = list_create();
	int tamanio;

	void* buffer = recibir_buffer(&size, socket_cliente);
	//int valor = *(int*)buffer;
	//int* del_punter_coso;

	// Copiamos el tamaño del siguiente bloque (en bytes) desde el buffer
    memcpy(&tamanio, (int*)buffer + desplazamiento, sizeof(int));
    desplazamiento += sizeof(int);

	// Reservamos memoria para el entero y copiamos desde el buffer
    int* valor = malloc(tamanio);
    memcpy(valor, (int*)buffer + desplazamiento, tamanio);
    desplazamiento += tamanio;

	list_add(valores, valor);

	free(buffer);
	return valores;
}




t_paquete* crear_super_paquete(op_code code_op){
	t_paquete* super_paquete = malloc(sizeof(t_paquete));
	super_paquete->codigo_operacion = code_op;
	crear_buffer(super_paquete);
	return  super_paquete;
}

void cargar_int_al_super_paquete(t_paquete* paquete, int numero){
	if(paquete->buffer->size == 0){
		paquete->buffer->stream = malloc(sizeof(int));
		memcpy(paquete->buffer->stream, &numero, sizeof(int));
	}else{
		paquete->buffer->stream = realloc(paquete->buffer->stream,
											paquete->buffer->size + sizeof(int));
		/**/
		memcpy(paquete->buffer->stream + paquete->buffer->size, &numero, sizeof(int));
	}

	paquete->buffer->size += sizeof(int);
}

void cargar_string_al_super_paquete(t_paquete* paquete, char* string){
	int size_string = strlen(string)+1;

	if(paquete->buffer->size == 0){
		paquete->buffer->stream = malloc(sizeof(int) + sizeof(char)*size_string);
		memcpy(paquete->buffer->stream, &size_string, sizeof(int));
		memcpy(paquete->buffer->stream + sizeof(int), string, sizeof(char)*size_string);

	}else {
		paquete->buffer->stream = realloc(paquete->buffer->stream,
										paquete->buffer->size + sizeof(int) + sizeof(char)*size_string);
		/**/
		memcpy(paquete->buffer->stream + paquete->buffer->size, &size_string, sizeof(int));
		memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), string, sizeof(char)*size_string);

	}
	paquete->buffer->size += sizeof(int);
	paquete->buffer->size += sizeof(char)*size_string;
}

void cargar_choclo_al_super_paquete(t_paquete* paquete, void* choclo, int size){
	if(paquete->buffer->size == 0){
		paquete->buffer->stream = malloc(sizeof(int) + size);
		memcpy(paquete->buffer->stream, &size, sizeof(int));
		memcpy(paquete->buffer->stream + sizeof(int), choclo, size);
	}else{
		paquete->buffer->stream = realloc(paquete->buffer->stream,
												paquete->buffer->size + sizeof(int) + size);

		memcpy(paquete->buffer->stream + paquete->buffer->size, &size, sizeof(int));
		memcpy(paquete->buffer->stream + paquete->buffer->size + sizeof(int), choclo, size);
	}

	paquete->buffer->size += sizeof(int);
	paquete->buffer->size += size;
}

int recibir_int_del_buffer(t_buffer* coso){
	if(coso->size == 0){
		printf("\n[ERROR] Al intentar extraer un INT de un t_buffer vacio\n\n");
		exit(EXIT_FAILURE);
	}

	if(coso->size < 0){
		printf("\n[ERROR] Esto es raro. El t_buffer contiene un size NEGATIVO \n\n");
		exit(EXIT_FAILURE);
	}

	int valor_a_devolver;
	memcpy(&valor_a_devolver, coso->stream, sizeof(int));

	int nuevo_size = coso->size - sizeof(int);
	if(nuevo_size == 0){
		free(coso->stream);
		coso->stream = NULL;
		coso->size = 0;
		return valor_a_devolver;
	}
	if(nuevo_size < 0){
		printf("\n[ERROR_INT]: BUFFER CON TAMAÑO NEGATIVO\n\n");
		//free(valor_a_devolver);
		//return 0;
		exit(EXIT_FAILURE);
	}
	void* nuevo_coso = malloc(nuevo_size);
	memcpy(nuevo_coso, coso->stream + sizeof(int), nuevo_size);
	free(coso->stream);
	coso->stream = nuevo_coso;
	coso->size = nuevo_size;

	return valor_a_devolver;
}

char* recibir_string_del_buffer(t_buffer* coso){
	//Fomrato de charGPT
// if(coso->size < sizeof(int)) {
//        perror("[ERROR]: Buffer demasiado pequeño para contener el tamaño de la cadena");
//        exit(EXIT_FAILURE);
//    }
//
//    int size_string;
//    char* string;
//    memcpy(&size_string, coso->stream, sizeof(int));
//
//    // Verifica que el tamaño de la cadena sea válido y que el buffer sea suficiente
//    if(size_string <= 0 || coso->size < sizeof(int) + size_string) {
//        perror("[ERROR]: Tamaño de cadena inválido o buffer demasiado pequeño para contener la cadena");
//        exit(EXIT_FAILURE);
//    }
//
//    string = malloc(size_string + 1);  // +1 para el '\0' al final
//    if (!string) {
//        perror("[ERROR]: No se pudo asignar memoria para la cadena");
//        exit(EXIT_FAILURE);
//    }
//
//    memcpy(string, coso->stream + sizeof(int), size_string);
//    string[size_string] = '\0';  // Asegurarte de que la cadena esté terminada en null
//
//    // ... el resto del código para manejar la reducción del tamaño del buffer ...
//
//    return string;

    //----------------- Formato Inicial----------------------------
	if(coso->size == 0){
		printf("\n[ERROR] Al intentar extraer un contenido de un t_buffer vacio\n\n");
		exit(EXIT_FAILURE);
	}

	if(coso->size < 0){
		printf("\n[ERROR] Esto es raro. El t_buffer contiene un size NEGATIVO \n\n");
		exit(EXIT_FAILURE);
	}

	int size_string;
	char* string;
	memcpy(&size_string, coso->stream, sizeof(int));
	//string = malloc(sizeof(size_string));
	string = malloc(size_string);
	memcpy(string, coso->stream + sizeof(int), size_string);

	int nuevo_size = coso->size - sizeof(int) - size_string;
	if(nuevo_size == 0){
		free(coso->stream);
		coso->stream = NULL;
		coso->size = 0;
		return string;
	}
	if(nuevo_size < 0){
		printf("\n[ERROR_STRING]: BUFFER CON TAMAÑO NEGATIVO\n\n");
		free(string);
		//return "[ERROR]: BUFFER CON TAMAÑO NEGATIVO";
		exit(EXIT_FAILURE);
	}
	void* nuevo_coso = malloc(nuevo_size);
	memcpy(nuevo_coso, coso->stream + sizeof(int) + size_string, nuevo_size);
	free(coso->stream);
	coso->stream = nuevo_coso;
	coso->size = nuevo_size;

	return string;
}

void* recibir_choclo_del_buffer(t_buffer* coso){
	if(coso->size == 0){
		printf("\n[ERROR] Al intentar extraer un contenido de un t_buffer vacio\n\n");
		exit(EXIT_FAILURE);
	}

	if(coso->size < 0){
		printf("\n[ERROR] Esto es raro. El t_buffer contiene un size NEGATIVO \n\n");
		exit(EXIT_FAILURE);
	}

	int size_choclo;
	void* choclo;
	memcpy(&size_choclo, coso->stream, sizeof(int));
	choclo = malloc(size_choclo);
	memcpy(choclo, coso->stream + sizeof(int), size_choclo);

	int nuevo_size = coso->size - sizeof(int) - size_choclo;
	if(nuevo_size == 0){
		free(coso->stream);
		coso->stream = NULL;
		coso->size = 0;
		return choclo;
	}
	if(nuevo_size < 0){
		printf("\n[ERROR_CHICLO]: BUFFER CON TAMAÑO NEGATIVO\n\n");
		//free(choclo);
		//return "";
		exit(EXIT_FAILURE);
	}
	void* nuevo_choclo = malloc(nuevo_size);
	memcpy(nuevo_choclo, coso->stream + sizeof(int) + size_choclo, nuevo_size);
	free(coso->stream);
	coso->stream = nuevo_choclo;
	coso->size = nuevo_size;

	return choclo;
}

t_buffer* recibiendo_super_paquete(int conexion){
	t_buffer* unBuffer = malloc(sizeof(t_buffer));
	int size;
	unBuffer->stream = recibir_buffer(&size, conexion);
	unBuffer->size = size;
	return unBuffer;
}





