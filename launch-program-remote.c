#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAX_RESPONSE_SIZE 4096
#define MAX_COMMAND_SIZE 1024
#define TIMEOUT 5

void* handle_client(void *arg) {
    puts("client connected");

    int socket = *((int *) arg);

    while (1) {
        char command[MAX_COMMAND_SIZE];
        memset(&command, 0, MAX_COMMAND_SIZE);
        int bytes_read = read(socket, command, MAX_COMMAND_SIZE);
        if (bytes_read < 1) {
            break;
        }

        if (strcmp(command, "\n") == 0) {
            continue;
        }

        int stdio_fd[2];
        if (pipe(stdio_fd) == -1) {
            perror("pipe failed");
            exit(EXIT_FAILURE);
        }

        int child_process_status;
        pid_t pid;
        if ((pid = fork()) == -1) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }

        int exec_status;
        if (pid == 0) {
            alarm(TIMEOUT);
            dup2(stdio_fd[1], STDOUT_FILENO);
            dup2(stdio_fd[1], STDERR_FILENO);
            exec_status = execlp("/bin/sh", "sh", "-c", command, NULL);
            perror("exec command failed");
            exit(EXIT_FAILURE);
        }

        waitpid(pid, &child_process_status, 0);

        if (WIFSIGNALED(child_process_status)) {
            char timeout_buff[MAX_RESPONSE_SIZE];
            memset(&timeout_buff, 0, MAX_RESPONSE_SIZE);

            sprintf(timeout_buff, "Execution timeout with status: %d", WTERMSIG(child_process_status));

            int bytes_sent = send(socket, timeout_buff, MAX_RESPONSE_SIZE, 0);
            if (bytes_sent < 0) {
                puts("client notification about timeout disconnected");
            }

            close(stdio_fd[0]);
            close(stdio_fd[1]);
            continue;
        };

        char result_buff[MAX_RESPONSE_SIZE];
        memset(&result_buff, 0, MAX_RESPONSE_SIZE);
        size_t output_bytes_read = read(stdio_fd[0], result_buff, MAX_RESPONSE_SIZE);

        sprintf(result_buff + strlen(result_buff), "%s%d%s%s", "Exit code: ", exec_status, "\n", "\n");

        int bytes_sent = send(socket, result_buff, MAX_RESPONSE_SIZE, 0);
        if (bytes_sent < 0) {
            perror("send failed");
        }

        close(stdio_fd[0]);
        close(stdio_fd[1]);
    }

    close(socket);
    puts("client disconnected");
}

int main(int argc, char *argv[]) {
    int port;
    char *address = argv[1];

    if (argc < 3) {
        perror("usage: ./server $address $port\n");
        exit(EXIT_FAILURE);
    }

    if (sscanf(argv[2], "%i", &port) != 1) {
        perror("port should be a number\n");
        exit(EXIT_FAILURE);
    }

    pthread_t thread_id;
    int listen_socket = 0;
    struct sockaddr_in server_address;
    socklen_t server_address_size = sizeof(server_address);
    memset(&server_address, 0, server_address_size);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if(inet_pton (AF_INET, address, &server_address.sin_addr) != 1) {
        perror("address parsing failed\n");
        exit(EXIT_FAILURE);
    }

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == -1) {
        perror("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    int val = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(listen_socket, (struct sockaddr *) &server_address, server_address_size)) {
        perror("bind failed.\n");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_socket, 10) < 0) {
        perror("listen failed.\n");
        exit(EXIT_FAILURE);
    }

    while(1) {
        struct sockaddr_in client_address;
        socklen_t client_address_size = sizeof(client_address);
        memset(&client_address, 0, client_address_size);

        int client_socket = accept(listen_socket, (struct sockaddr*) &client_address, &client_address_size);
        if (client_socket < 0) {
            perror("accept failed.\n");
            exit(EXIT_FAILURE);
        }

        int created_thread = pthread_create(&thread_id, NULL, handle_client, &client_socket);
        if (created_thread != 0) {
            perror("create thread failed.\n");
            exit(EXIT_FAILURE);
        }
        pthread_detach(thread_id);
        sched_yield();
    }

    close(listen_socket);
    return 0;
}
