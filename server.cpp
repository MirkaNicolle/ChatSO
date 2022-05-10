/*
** Codigo de servidor. Parte del codigo esta basado en lo que leimos en la guia del maestro Beej:
** https://beej.us/guide/bgnet/html/
*/

#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <sys/socket.h>
//#include <winsock2.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <unordered_map>
#include "mensaje.pb.h"

#define BACKLOG 5
#define BUFFER_SIZE 8192
#define CLIENT_MSG_OPT_SYNC 1
#define CLIENT_MSG_OPT_USERS 2
#define CLIENT_MSG_OPT_STATUS 3
#define CLIENT_MSG_OPT_BROADCAST 4
#define CLIENT_MSG_OPT_DM 5
#define CLIENT_MSG_OPT_ACK 6

// Representacion de cliente
struct ChatClient
{
    int socketFd;
    std::string username;
    char ipAddr[INET_ADDRSTRLEN];
    std::string status;
};

// Guarda los clientes, se usa el username de cada cliente como llave
std::unordered_map<std::string, ChatClient *> clients;

// Estadisticas
long rxByteCount, txByteCount;
// Para cada send o recv
int bytesReceived, bytesSent;
int messagesReceived, messagesSent;

/*
* Imprime estadisticas del servidor.
*/
void PrintServerStats()
{
    std::cout << std::endl
              << "******************** Estadísticas de Servidor ********************"
              << std::endl
              << "* Usuarios conectados: " << clients.size() << std::endl
              << "* Bytes Recibidos: " << rxByteCount << std::endl
              << "* Bytes Enviados: " << txByteCount << std::endl
              << "* Mensajes Recibidos: " << messagesReceived << std::endl
              << "* Mensajes Enviados: " << messagesSent << std::endl
              << "******************************************************************"
              << std::endl
              << std::endl;
}

/*
* Envia ErrorResponse de vuelta a cliente identificado con socketFd.
*/
void SendErrorResponse(int socketFd, std::string errorMsg)
{
    std::string msgSerialized;
    chat::ErrorResponse *errorMessage = new chat::ErrorResponse();
    errorMessage->set_errormessage(errorMsg);
    chat::ServerMessage serverMessage;
    serverMessage.set_option(3);
    serverMessage.set_allocated_error(errorMessage);
    serverMessage.SerializeToString(&msgSerialized);
    char buffer[msgSerialized.size() + 1];
    strcpy(buffer, msgSerialized.c_str());
    bytesSent = send(socketFd, buffer, sizeof buffer, 0);
    txByteCount += bytesSent;
    messagesSent++;
}

/// Trabajo de cada thread, que se encarga de cada cliente
void *ThreadWork(void *params)
{
    // Utilizados por los distintos mensajes
    struct ChatClient thisClient;
    struct ChatClient *newClientParams = (struct ChatClient *)params;
    // int socketFd = *(int *)params;
    int socketFd = newClientParams->socketFd;
    char buffer[BUFFER_SIZE];
    std::string msgSerialized;
    chat::ClientMessage clientMessage;
    chat::ServerMessage serverMessage;

    // Mesaje de inicio
    std::cout << "Servidor: Hola! soy el thread que se encarga del cliente asociado al socket "
              << socketFd
              << std::endl;

    // main loop de cliente
    while (1)
    {
        // recibir mensaje de cliente
        if ((bytesReceived = recv(socketFd, buffer, BUFFER_SIZE, 0)) < 1)
        {
            if (bytesReceived == 0)
            {
                // cliente cerro conexion
                std::cout << "Servidor: cliente ("
                          << thisClient.username
                          << ") ha cerrado su sesión."
                          << std::endl;
            }
            else
            {
                // error
                perror("Servidor: ");
            }

            break;
        }

        // Para llevar estadisticas del servidor
        rxByteCount += bytesReceived;
        messagesReceived++;

        // Parsear mensaje recibido de cliente
        clientMessage.ParseFromString(buffer);

        // Un if para cada opcion del cliente
        if (clientMessage.option() == CLIENT_MSG_OPT_SYNC)
        {
            if (!clientMessage.has_synchronize())
            {
                SendErrorResponse(socketFd, "Cliente no envió informacion: synchronize");
                break;
            }

            // *********** TCP 3 Way Handshake Inicio ***********
            chat::MyInfoSynchronize myInfoSync = clientMessage.synchronize();
            std::cout << "Servidor: se recibió MyInfoSynchronize de: "
                      << myInfoSync.username()
                      << std::endl;

            // Revisar si nombre de usuario ya existe, si ya existe, se devuelve ErrorResponse.
            if (clients.count(myInfoSync.username()) > 0)
            {
                std::cout << "Servidor: el nombre de usuario ya existe." << std::endl;
                SendErrorResponse(socketFd, "El nombre de usuario ya existe.");
                break;
            }

            // llenar MyInfoResponse
            chat::MyInfoResponse *myInfoRes = new chat::MyInfoResponse;
            myInfoRes->set_userid(socketFd);

            // llenar ServerMessage
            serverMessage.Clear();
            serverMessage.set_option(4);
            serverMessage.set_allocated_myinforesponse(myInfoRes);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;
            std::cout << "Servidor: he enviado MyInfoResponse con userId: "
                      << socketFd
                      << std::endl;

            // Guardar informacion de nuevo cliente
            thisClient.username = myInfoSync.username();
            thisClient.socketFd = socketFd;
            thisClient.status = "ACTIVO";
            strcpy(thisClient.ipAddr, newClientParams->ipAddr);
            clients[thisClient.username] = &thisClient;
        }
        else if (clientMessage.option() == CLIENT_MSG_OPT_ACK)
        {
            std::cout << "Servidor: usuario (" << thisClient.username
                      << ") con ip (" << thisClient.ipAddr << ") registrado con exito!"
                      << std::endl;
        }
        else if (clientMessage.option() == CLIENT_MSG_OPT_USERS)
        {
            // Verificación de Request data
            if (!clientMessage.has_connectedusers())
            {
                std::cout << "Servidor: Cliente no envió informacion: connectedusers" << std::endl;
                SendErrorResponse(socketFd, "Cliente no envió informacion: connectedusers");
            }

            // El usuario quiere todos los usuarios, o solo datos de uno?
            if (clientMessage.connectedusers().has_userid() &&
                clientMessage.connectedusers().userid() == 0)
            {
                std::cout << "Servidor: usuario (" << thisClient.username
                          << ") ha solicitado lista de todos (" << clients.size()
                          << ") usuarios. " << std::endl;
                // iterar por todos los clientes y crear nuestro Response
                chat::ConnectedUserResponse *connUsersRes = new chat::ConnectedUserResponse();
                for (auto item = clients.begin(); item != clients.end(); ++item)
                {
                    chat::ConnectedUser *user = connUsersRes->add_connectedusers();
                    user->set_userid(item->second->socketFd);
                    user->set_username(item->first);
                    user->set_ip(item->second->ipAddr);
                    user->set_status(item->second->status);
                }

                // llenar ServerMessage
                serverMessage.Clear();
                serverMessage.set_option(5);
                serverMessage.set_allocated_connecteduserresponse(connUsersRes);
                serverMessage.SerializeToString(&msgSerialized);

                // enviar por socket
                strcpy(buffer, msgSerialized.c_str());
                bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
                txByteCount += bytesSent;
                messagesSent++;
            }
            else if (clientMessage.connectedusers().has_username())
            {
                std::cout << "Servidor: usuario (" << thisClient.username
                          << ") ha solicitado detalles de usuario ("
                          << clientMessage.connectedusers().username()
                          << ")" << std::endl;

                // Verificar que nombre de usuario exista
                if (clients.count(clientMessage.connectedusers().username()) > 0)
                {
                    chat::ConnectedUserResponse *connUsersRes = new chat::ConnectedUserResponse();
                    chat::ConnectedUser *user = connUsersRes->add_connectedusers();
                    struct ChatClient *reqClient = clients[clientMessage.connectedusers().username()];
                    user->set_userid(reqClient->socketFd);
                    user->set_username(reqClient->username);
                    user->set_ip(reqClient->ipAddr);
                    user->set_status(reqClient->status);

                    // llenar ServerMessage
                    serverMessage.Clear();
                    serverMessage.set_option(5);
                    serverMessage.set_allocated_connecteduserresponse(connUsersRes);
                    serverMessage.SerializeToString(&msgSerialized);

                    // enviar por socket
                    strcpy(buffer, msgSerialized.c_str());
                    bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
                    txByteCount += bytesSent;
                    messagesSent++;
                }
                else
                {
                    std::cout << "Servidor: No existe el nombre de usuario solicitado: connectedusers" << std::endl;
                    SendErrorResponse(socketFd, "No existe el nombre de usuario especificado.");
                }
            }
            else
            {
                SendErrorResponse(socketFd, "Cliente no envió información: connectedusers: userId / username");
            }
        }
        else if (clientMessage.option() == CLIENT_MSG_OPT_STATUS)
        {
            if (!clientMessage.has_changestatus())
            {
                std::cout << "Servidor: Cliente no envió información: chagestatus" << std::endl;
                SendErrorResponse(socketFd, "Cliente no envió información: changestatus");
                continue;
            }

            chat::ChangeStatusRequest chngStatReq = clientMessage.changestatus();
            std::cout << "Servidor: usuario (" << thisClient.username
                      << ") ha solicitado un nuevo estado: "
                      << chngStatReq.status()
                      << std::endl;

            // actualizar status
            std::string s0 = "ACTIVO";
            std::string s1 = "INACTIVO";
            std::string s2 = "OCUPADO";

            if (chngStatReq.status().compare(s0) != 0 && chngStatReq.status().compare(s1) != 0 && chngStatReq.status().compare(s2) != 0)
            {
                std::cout << "Servidor: estado inválido: changestatus" << std::endl;
                SendErrorResponse(socketFd, "Nuevo estado inválido: changestatus");
                continue;
            }

            // Actualizar estado de cliente
            thisClient.status = chngStatReq.status();

            // Respuesta de Servidor con nuevo estado
            chat::ChangeStatusResponse *chngStatRes = new chat::ChangeStatusResponse();
            chngStatRes->set_userid(socketFd);
            chngStatRes->set_status(thisClient.status);

            // llenar ServerMessage
            serverMessage.Clear();
            serverMessage.set_option(6);
            serverMessage.set_allocated_changestatusresponse(chngStatRes);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;
        }
        else if (clientMessage.option() == CLIENT_MSG_OPT_BROADCAST)
        {
            if (!clientMessage.has_broadcast())
            {
                std::cout << "Servidor: Cliente no envió información: broadcast" << std::endl;
                SendErrorResponse(socketFd, "Cliente no envió información: broadcast");
            }

            // BroadcastRequest, trae mensaje de usuario
            chat::BroadcastRequest brdReq = clientMessage.broadcast();
            std::cout << "Servidor: usuario (" << thisClient.username
                      << ") desea enviar un mensaje (BROADCAST)\n    "
                      << brdReq.message() << std::endl;

            // Primero se envia el estado del mensaje, unicamente al usuario que
            // solicito el broadcast.
            chat::BroadcastResponse *brdRes = new chat::BroadcastResponse();
            brdRes->set_messagestatus("RECIBIDO EN SERVER");

            // Llenar ServerMessage
            serverMessage.Clear();
            serverMessage.set_option(7);
            serverMessage.set_allocated_broadcastresponse(brdRes);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;

            // Luego, se envia el mensaje a todos los usuarios, menos al que
            // lo solicito.
            chat::BroadcastMessage *brdMsg = new chat::BroadcastMessage();
            brdMsg->set_message(brdReq.message());
            brdMsg->set_userid(socketFd);
            brdMsg->set_username(thisClient.username);

            serverMessage.Clear();
            serverMessage.set_option(1);
            serverMessage.set_allocated_broadcast(brdMsg);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket a todos los usuarios, menos a mi mismo
            strcpy(buffer, msgSerialized.c_str());
            for (auto item = clients.begin(); item != clients.end(); ++item)
            {
                if (item->first != thisClient.username)
                {
                    bytesSent = send(item->second->socketFd, buffer, msgSerialized.size() + 1, 0);
                    txByteCount += bytesSent;
                    messagesSent++;
                }
            }
        }
        else if (clientMessage.option() == CLIENT_MSG_OPT_DM)
        {
            if (!clientMessage.has_directmessage() || !clientMessage.directmessage().has_username())
            {
                std::cout << "Servidor: Cliente no envió información: directmessage" << std::endl;
                SendErrorResponse(socketFd, "Cliente no envió información: directmessage");
                continue;
            }

            // Validar que usuario destinatario exista y este conectado
            if (clients.count(clientMessage.directmessage().username()) < 1)
            {
                std::cout << "Servidor: destinatario no existe o no está conectado: directmessage" << std::endl;
                SendErrorResponse(socketFd, "destinatario no existe o no está conectado: directmessage");
                continue;
            }

            // Obtener mensaje de request
            chat::DirectMessageRequest dirMsg = clientMessage.directmessage();
            std::cout << "Servidor: usuario (" << thisClient.username
                      << ") envía mensaje privado a " << dirMsg.username()
                      << ". El mensaje es: \n    " << dirMsg.message()
                      << std::endl;

            // Notificar a usuario que su mensaje fue recibido en el servidor
            chat::DirectMessageResponse *dirMsgRes = new chat::DirectMessageResponse();
            dirMsgRes->set_messagestatus("RECIBIDO EN SERVER");

            // Llenar ServerMessage
            serverMessage.Clear();
            serverMessage.set_option(8);
            serverMessage.set_allocated_directmessageresponse(dirMsgRes);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;

            // Enviar mensaje a destinatario
            chat::DirectMessage *dirMsgSend = new chat::DirectMessage();
            dirMsgSend->set_message(dirMsg.message());
            dirMsgSend->set_userid(thisClient.socketFd);
            dirMsgSend->set_username(thisClient.username);

            serverMessage.Clear();
            serverMessage.set_option(2);
            serverMessage.set_allocated_message(dirMsgSend);
            serverMessage.SerializeToString(&msgSerialized);

            // enviar por socket
            int destSocket = clients[clientMessage.directmessage().username()]->socketFd;
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(destSocket, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;
        }
        else
        {
            // error - opcion incorrecta
            chat::ErrorResponse *errorRes = new chat::ErrorResponse();
            errorRes->set_errormessage("Error en petición al servidor: opción incorrecta.");
            serverMessage.Clear();
            serverMessage.set_option(3);
            serverMessage.set_allocated_error(errorRes);
            serverMessage.SerializeToString(&msgSerialized);
            strcpy(buffer, msgSerialized.c_str());
            bytesSent = send(socketFd, buffer, msgSerialized.size() + 1, 0);
            txByteCount += bytesSent;
            messagesSent++;
        }

        PrintServerStats();
    }

    // limpiar mapa, cerrar socket, terminar thread
    clients.erase(thisClient.username);
    close(socketFd);
    std::string tmpUsername = thisClient.username;
    if (tmpUsername.empty())
        tmpUsername = "SIN REGISTRAR";
    std::cout << "Servidor: socket de " << tmpUsername
              << " cerrado. Terminando ejecución de thread dedicado."
              << std::endl;
    pthread_exit(0);
}

int main(int argc, char *argv[])
{
    // Verificación de Protobuf
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (argc != 2)
    {
        fprintf(stderr, "Uso: server <puertodelservidor>\n");
        return 1;
    }

    // convertir numero de puerto
    long port = strtol(argv[1], NULL, 10);

    sockaddr_in server, incoming_conn;
    socklen_t new_conn_size;
    int socket_fd, new_conn_fd;
    char incoming_conn_addr[INET_ADDRSTRLEN];

    // configuracion de socket de server
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(server.sin_zero, 0, sizeof server.sin_zero);

    // obtener socket fd
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "Servidor: error creando socket.\n");
        return 1;
    }

    // bindear address y puerto a socket
    if (bind(socket_fd, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
        close(socket_fd);
        fprintf(stderr, "Servidor: error bindeando IP:Puerto a socket.\n");
        return 2;
    }

    // escuchar conexiones, cola maxima de BACKLOG
    if (listen(socket_fd, BACKLOG) == -1)
    {
        close(socket_fd);
        fprintf(stderr, "Servidor: error en listen().\n");
        return 3;
    }
    printf("Servidor: escuchando en puerto %ld\n", port);

    // loop para aceptar conexiones
    while (1)
    {
        new_conn_size = sizeof incoming_conn;
        new_conn_fd = accept(socket_fd, (struct sockaddr *)&incoming_conn, &new_conn_size);
        if (new_conn_fd == -1)
        {
            perror("error en accept()");
            continue;
        }

        // nuevo cliente
        struct ChatClient newClient;
        newClient.socketFd = new_conn_fd;
        inet_ntop(AF_INET, &(incoming_conn.sin_addr), newClient.ipAddr, INET_ADDRSTRLEN);

        // despachar thread de nuevo cliente
        pthread_t thread_id;
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        // pthread_create(&thread_id, &attrs, ThreadWork, (void *)&new_conn_fd);
        pthread_create(&thread_id, &attrs, ThreadWork, (void *)&newClient);
    }

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
