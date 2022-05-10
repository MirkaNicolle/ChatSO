/*
** Codigo de cliente. Parte del codigo esta basado en lo que leimos en la guia del maestro Beej:
** https://beej.us/guide/bgnet/html/
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
//#include <winsock2.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ifaddrs.h>
#include "mensaje.pb.h"
#include <queue>

int connected, waitingForServerResponse, waitingForInput;
std::queue<std::string> msgQueue;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET)
	{
		return &(((struct sockaddr_in *)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void *listenToMessages(void *args)
{
	while (1)
	{
		char bufferMsg[8192];
		int *sockmsg = (int *)args;
		chat::ServerMessage serverMsg;

		// recibir response de servidor
		int bytesReceived = recv(*sockmsg, bufferMsg, 8192, 0);

		// Parsear mensaje de servidor
		serverMsg.ParseFromString(bufferMsg);

		// ifs para manejar opciones de servidor
		if (serverMsg.option() == 1)
		{
			if (waitingForInput)
				msgQueue.push("Mensaje broadcast (pendiente) recibido de " + serverMsg.broadcast().username() + ": " + serverMsg.broadcast().message());
			else
			{
				auto msg = serverMsg.broadcast().message().c_str();
				auto username = serverMsg.broadcast().username().c_str();

				printf("Mensaje broadcast recibido de %s: %s \n", username, msg);
			}
		}
		// si es 2, es DM
		else if (serverMsg.option() == 2)
		{
			if (waitingForInput)
				msgQueue.push("Mensaje privado (pendiente) recibido: " + serverMsg.message().username() + ": " + serverMsg.message().message());
			else
			{
				auto msg = serverMsg.message().message().c_str();
				auto username = serverMsg.message().username().c_str();

				printf("Mensaje privado recibido de %s: %s \n", username, msg);
			}
		}
		else if (serverMsg.option() == 3)
		{
			// ERROR
			std::cout << "Cliente: servidor indica que hubo error: "
					  << serverMsg.error().errormessage() << std::endl;
		}
		else if (serverMsg.option() == 4)
		{
			// no se utiliza
		}
		else if (serverMsg.option() == 5)
		{
			/* RECIBIR RESPUESTA DE SERVIDOR */
			// 	/* PROCESAR DATOS DE USUARIOS CONECTADOS */
			int user_count = serverMsg.connecteduserresponse().connectedusers().size();

			printf("------------------------------------------------------------------------------------\n");
			printf("|      ID       |            USUARIO            |       IP        |     ESTADO     |\n");
			printf("------------------------------------------------------------------------------------\n");

			for (int i = 0; i < user_count; i++)
			{
				auto user = serverMsg.connecteduserresponse().connectedusers(i);

				// imprimir user id
				int strSize = user.userid() > 10 ? 2 : 1;
				printf("| %d", user.userid());
				for (int i = 0; i < 14 - strSize; i++)
					printf(" ");

				// imprimir username
				strSize = strlen(user.username().c_str());
				printf("| %s", user.username().c_str());
				for (int i = 0; i < 30 - strSize; i++)
					printf(" ");

				// imprimir ip
				strSize = strlen(user.ip().c_str());
				printf("| %s", user.ip().c_str());
				for (int i = 0; i < 16 - strSize; i++)
					printf(" ");

				// imprimir estado
				strSize = strlen(user.status().c_str());
				printf("| %s", user.status().c_str());
				for (int i = 0; i < 14 - strSize; i++)
					printf(" ");
				printf(" |\n");
			}

			printf("------------------------------------------------------------------------------------\n");
		}
		else if (serverMsg.option() == 6)
		{
			printf("Estado actualizado con exito! Nuevo estado: %s\n",
				   serverMsg.changestatusresponse().status().c_str());
		}
		else if (serverMsg.option() == 7)
		{
			printf("Mensaje broadcast recibido en servidor.\n");
		}
		else if (serverMsg.option() == 8)
		{
			printf("Mensaje privado recibido en servidor.\n");
		}
		else
		{
			// error: respuesta no reconocida
			printf("Se recibio una respuesta invalida del servidor.\n");
		}

		while (!waitingForInput && !msgQueue.empty())
		{
			printf("%s\n", msgQueue.front().c_str());
			msgQueue.pop();
		}

		// notificar a main thread que ya puede seguir
		waitingForServerResponse = 0;

		if (connected == 0)
		{
			pthread_exit(0);
		}
	}
}

void print_menu(char *usrname)
{
	printf("------------------------------------------------------- \n");
	printf("Bienvenido al lobby, %s. \n", usrname);
	printf("1. Enviar un mensaje a todos los usuarios. \n");
	printf("2. Enviar mensaje directo. \n");
	printf("3. Cambiar status. \n");
	printf("4. Obtener lista de usuarios conectados. \n");
	printf("5. Desplegar informacion de usuario. \n");
	printf("6. Ayuda. \n");
	printf("7. Salir. \n");
	printf("------------------------------------------------------- \n");
}

void print_help_menu() {
	printf("------------------------------------------------------- \n");
	printf("AYUDA \n");
	printf("Opcion 1 (Broadcast): Enviar un mensaje a todos los usuarios conectados actualmente a la sala. \n");
	printf("Opcion 2 (Direct Message): Enviar un mensaje por privado a un recipiente conectado actualmente a la sala. \n");
	printf("Opcion 3 (Cambiar estado): Permite cambiar el estado del usuario para indicar su actividad en la sala. \n");
	printf("Opcion 4 (Obtener lista de usuarios): Imprime todos los usuarios conectados actualmente a la sala, con su userId y username. \n");
	printf("Opcion 5 (Desplegar informacion de usuario): Imprime los datos de un usuario en especifico, incluendo su username y estado. \n");
	printf("Opcion 7 (Salir): Permite que el usuario salga del sistema y cerrar conexion con el servidor. \n");
	printf("------------------------------------------------------- \n");
}

int get_client_option()
{
	// Client options
	int client_opt;
	std::cin >> client_opt;

	while (std::cin.fail())
	{
		std::cout << "Por favor, ingrese una opcion valida: " << std::endl;
		std::cin.clear();
		std::cin.ignore(256, '\n');
		std::cin >> client_opt;
	}

	return client_opt;
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;
	char buf[8192];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	char *usrname;

	if (argc != 4)
	{
		fprintf(stderr, "usage: client <username> <server_ip> <server_port>\n");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[2], argv[3], &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
							 p->ai_protocol)) == -1)
		{
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			perror("client: connect");
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL)
	{
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			  s, sizeof s);
	printf("Cliente: conectado con %s\n", s);
	freeaddrinfo(servinfo); // all done with this structure

	// escribir mensaje
	char buffer[8192];
	std::string message_serialized;

	chat::UserRegistration *syncMessage = new chat::UserRegistration;
	syncMessage->set_username(argv[1]);
	syncMessage->set_ip("");

	chat::ClientMessage clientMessage;
	clientMessage.set_option(1);
	clientMessage.set_allocated_synchronize(syncMessage);
	clientMessage.SerializeToString(&message_serialized);
	clientMessage.mutable_synchronize();

	std::cout << "Cliente: size "
			  << message_serialized.size()
			  << std::endl;

	strcpy(buffer, message_serialized.c_str());
	send(sockfd, buffer, message_serialized.size() + 1, 0);

	// recibir response de servidor
	recv(sockfd, buffer, 8192, 0);

	chat::ServerMessage serverMessage;
	serverMessage.ParseFromString(buffer);
	std::cout << "Cliente: recibi MyInfoResponse. Mi userId es "
			  << serverMessage.myinforesponse().userid()
			  << std::endl;

	// ack
	chat::MyInfoAcknowledge *ackReq = new chat::MyInfoAcknowledge;
	ackReq->set_userid(serverMessage.myinforesponse().userid());
	clientMessage.Clear();
	clientMessage.set_option(6);
	clientMessage.set_allocated_acknowledge(ackReq);
	clientMessage.SerializeToString(&message_serialized);
	strcpy(buffer, message_serialized.c_str());
	send(sockfd, buffer, message_serialized.size() + 1, 0);

	connected = 1;
	usrname = argv[1];

	// despachar thread que escucha mensajes del server
	pthread_t thread_id;
	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_create(&thread_id, &attrs, listenToMessages, (void *)&sockfd);

	print_menu(usrname);
	int client_opt;

	while (client_opt != 8)
	{
		while (waitingForServerResponse == 1)
		{
			// para no mezclar outputs de mensajes recibidos con input del usuario.
		}

		printf("\nEscoja una opcion a realizar (AYUDA = 6):\n");
		client_opt = get_client_option();

		std::string msgSerialized;
		int bytesReceived, bytesSent;

		// Broadcasting
		if (client_opt == 1)
		{
			waitingForInput = 1;
			printf("Ingrese un mensaje a enviar: \n");
			std::cin.ignore();
			std::string msg;
			std::getline(std::cin, msg);

			/* ENVIAR SOLICITUD A SERVIDOR */
			chat::ClientMessage clientMsg;
			chat::BroadcastRequest *brdReq = new chat::BroadcastRequest();

			// Se envia opcion 4: BroadcastRequest
			clientMsg.set_option(4);
			brdReq->set_message(msg);
			clientMsg.set_allocated_broadcast(brdReq);
			clientMsg.SerializeToString(&msgSerialized);

			// enviar por socket
			strcpy(buffer, msgSerialized.c_str());
			bytesSent = send(sockfd, buffer, msgSerialized.size() + 1, 0);
			waitingForServerResponse = 1;
			waitingForInput = 0;
		}
		else if (client_opt == 2)
		{
			printf("Ingrese el nombre de usuario al que quiere enviarle un mensaje: \n");
			std::cin.ignore();
			std::string user_name;
			std::getline(std::cin, user_name);

			printf("Ingrese un mensaje a enviar: \n");
			std::cin.ignore();
			std::string msg;
			std::getline(std::cin, msg);

			/* ENVIAR SOLICITUD A SERVIDOR */
			chat::ClientMessage clientMsg;
			chat::Message *dmReq = new chat::Message();

			// Se envia opcion 5: DirectMessageRequest
			clientMsg.set_option(5);
			dmReq->set_username(user_name);
			dmReq->set_message(msg);
			clientMsg.set_allocated_directmessage(dmReq);
			clientMsg.SerializeToString(&msgSerialized);

			// enviar por socket
			strcpy(buffer, msgSerialized.c_str());
			bytesSent = send(sockfd, buffer, msgSerialized.size() + 1, 0);
			waitingForServerResponse = 1;
		}
		// Cambio de status
		else if (client_opt == 3)
		{
			// Preguntar por nuevo status
			printf("Ingrese su nuevo estado: \n");
			printf("1. ACTIVO\n");
			printf("2. OCUPADO\n");
			printf("3. INACTIVO\n");
			int option;
			std::cin >> option;
			std::string newStatus;
			if (option == 1)
				newStatus = "ACTIVO";
			else if (option == 2)
				newStatus = "OCUPADO";
			else if (option == 3)
				newStatus = "INACTIVO";
			else
			{
				printf("Estado invalido.\n");
				continue;
			}

			/* ENVIAR SOLICITUD A SERVIDOR */
			chat::ClientMessage clientMsg;
			chat::ChangeStatus *statReq = new chat::ChangeStatus();

			// Se envia opcion 3: ChangeStatus
			clientMsg.set_option(3);
			statReq->set_status(newStatus);
			clientMsg.set_allocated_changestatus(statReq);
			clientMsg.SerializeToString(&msgSerialized);

			// enviar por socket
			strcpy(buffer, msgSerialized.c_str());
			bytesSent = send(sockfd, buffer, msgSerialized.size() + 1, 0);
			waitingForServerResponse = 1;
		}
		// Obtener lista de usuarios
		else if (client_opt == 4)
		{
			/* ENVIAR SOLICITUD A SERVIDOR */
			chat::ClientMessage clientMsg;
			chat::connectedUserRequest *connUsrReq = new chat::connectedUserRequest();
			// Error en definicion de protocolo, pero se arregla aqui.
			// Se envia opcion 2:  connectedUsers
			clientMsg.set_option(2);
			// Debido a que queremos toda la lista de usuarios, configuramos UserId = 0, debido a que se definio de esta manera en el protocolo.
			connUsrReq->set_userid(0);
			clientMsg.set_allocated_connectedusers(connUsrReq);
			clientMsg.SerializeToString(&msgSerialized);

			// enviar por socket
			strcpy(buffer, msgSerialized.c_str());
			bytesSent = send(sockfd, buffer, msgSerialized.size() + 1, 0);
			waitingForServerResponse = 1;
		}

		// Obtener info sobre usuario en especifico
		else if (client_opt == 5)
		{
			// Preguntar por user_id
			std::string user_name;
			printf("Ingrese el nombre del usuario que quiere consultar: \n");
			std::cin >> user_name;

			/* ENVIAR SOLICITUD A SERVIDOR */
			chat::ClientMessage clientMsg;
			chat::connectedUserRequest *connUsrReq = new chat::connectedUserRequest();

			// Error en definicion de protocolo, pero se arregla aqui.
			// Se envia opcion 2:  connectedUsers
			clientMsg.set_option(2);

			// Mandamos el username que queremos consultar
			connUsrReq->set_username(user_name);
			clientMsg.set_allocated_connectedusers(connUsrReq);
			clientMsg.SerializeToString(&msgSerialized);

			// enviar por socket
			strcpy(buffer, msgSerialized.c_str());
			bytesSent = send(sockfd, buffer, msgSerialized.size() + 1, 0);
			waitingForServerResponse = 1;
		}
		else if (client_opt == 6)
		{
			// ayuda
			print_help_menu();
		}
		else if (client_opt == 7)
		{
			int option;
			printf("Cerrar sesion? \n");
			printf("1. Si \n");
			printf("2. No \n");

			std::cin >> option;

			if (option == 1)
			{
				printf("Gracias por utilizar el sistema. \n");
				break;
			}
		}
		else
		{
			std::cout << "Opción inválida." << std::endl;
		}
	}

	// cerrar conexion
	pthread_cancel(thread_id);
	connected = 0;
	close(sockfd);

	return 0;
}
