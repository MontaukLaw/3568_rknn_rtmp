
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
static int sock;
extern bool start_process;
void *recv_messages(void *arg)
{
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (true)
    {
        bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0';
            std::cout << "Received: >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> " << buffer << std::endl;

            std::string message(buffer);
            if (message.rfind("CMD", 0) == 0 && message.find("CMD", message.length() - 3) != std::string::npos)
            {
                std::cout << "Received CMD message: " << message << std::endl;

                if (message.find("CMD_STOP") != std::string::npos)
                {
                    if (start_process)
                    {
                        std::cout << "Received CMD:STOP message, stoping..." << std::endl;
                        start_process = false;
                    }
                }
                else if (message.find("CMD_START") != std::string::npos)
                {
                    if (start_process == false)
                    {
                        start_process = true;
                        std::cout << "Received CMD:START message, starting..." << std::endl;
                    }
                }
            }
            else
            {
                std::cout << "Received (non-CMD): " << message << std::endl;
            }
        }
        else if (bytes_read == 0)
        {
            std::cout << "The server closed the connection." << std::endl;
            break;
        }
        else
        {
            std::cerr << "Recv failed: " << strerror(errno) << std::endl;
            break;
        }

        usleep(10);
    }

    close(sock);
    return NULL;
}

void init_cmd_client(const char *server_ip, int port)
{

    struct sockaddr_in server_addr;
    pthread_t thread_id;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return;
    }

    // Server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Connect failed: " << strerror(errno) << std::endl;
        return;
    }
    std::cout << "::::::::::::::::::: Connected to the server." << std::endl;

    printf("server init start pthread now\n");

    if (pthread_create(&thread_id, NULL, recv_messages, (void *)&sock) != 0)
    {
        std::cerr << "Failed to create thread: " << strerror(errno) << std::endl;
    }
}
