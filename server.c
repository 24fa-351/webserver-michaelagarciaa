#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 4096

int request_count = 0;
int total_received_bytes = 0;
int total_sent_bytes = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *client_socket);
void serve_static_file(int client_socket, const char *path);
void serve_stats(int client_socket);
void serve_calc(int client_socket, const char *query_string);
int parse_query(const char *query, const char *key);

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listening failed");
        close(server_socket);
        exit(1);
    }

    printf("Server listening on port %d\n", port);

    while (1) {
        addr_size = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket < 0) {
            perror("Client accept failed");
            continue;
        }

        pthread_t thread;
        int *client_sock_ptr = malloc(sizeof(int));
        *client_sock_ptr = client_socket;
        pthread_create(&thread, NULL, handle_client, client_sock_ptr);
        pthread_detach(thread);
    }

    close(server_socket);
    return 0;
}

void *handle_client(void *client_socket) {
    int client_sock = *(int*)client_socket;
    free(client_socket);

    char buffer[BUFFER_SIZE];
    int received_bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (received_bytes <= 0) {
        close(client_sock);
        return NULL;
    }

    pthread_mutex_lock(&stats_mutex);
    request_count++;
    total_received_bytes += received_bytes;
    pthread_mutex_unlock(&stats_mutex);

    buffer[received_bytes] = '\0';
    char method[16], path[256], protocol[16];
    sscanf(buffer, "%s %s %s", method, path, protocol);

    if (strcmp(method, "GET") == 0) {
        if (strncmp(path, "/static/", 8) == 0) {
            serve_static_file(client_sock, path);
        } else if (strcmp(path, "/stats") == 0) {
            serve_stats(client_sock);
        } else if (strncmp(path, "/calc?", 6) == 0) {
            serve_calc(client_sock, path + 6);
        } else {
            send(client_sock, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
        }
    } else {
        send(client_sock, "HTTP/1.1 405 Method Not Allowed\r\n\r\n", 36, 0);
    }

    close(client_sock);
    return NULL;
}

void serve_static_file(int client_socket, const char *path) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), ".%s", path);
    
    int file = open(file_path, O_RDONLY);
    if (file < 0) {
        send(client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", 26, 0);
        return;
    }

    struct stat file_stat;
    fstat(file, &file_stat);
    int file_size = file_stat.st_size;

    char header[BUFFER_SIZE];
    int header_len = snprintf(header, sizeof(header), 
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Length: %d\r\n\r\n", 
                              file_size);
    send(client_socket, header, header_len, 0);

    int sent_bytes = 0, bytes_read;
    while ((bytes_read = read(file, header, BUFFER_SIZE)) > 0) {
        send(client_socket, header, bytes_read, 0);
        sent_bytes += bytes_read;
    }
    close(file);

    pthread_mutex_lock(&stats_mutex);
    total_sent_bytes += sent_bytes;
    pthread_mutex_unlock(&stats_mutex);
}

void serve_stats(int client_socket) {
    char response[BUFFER_SIZE];
    int response_len;

    pthread_mutex_lock(&stats_mutex);
    response_len = snprintf(response, sizeof(response),
                            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body>"
                            "<h1>Server Statistics</h1>"
                            "<p>Requests received: %d</p>"
                            "<p>Total bytes received: %d</p>"
                            "<p>Total bytes sent: %d</p>"
                            "</body></html>",
                            request_count, total_received_bytes, total_sent_bytes);
    pthread_mutex_unlock(&stats_mutex);

    send(client_socket, response, response_len, 0);
}

void serve_calc(int client_socket, const char *query_string) {
    int a = parse_query(query_string, "a");
    int b = parse_query(query_string, "b");
    int sum = a + b;

    char response[BUFFER_SIZE];
    int response_len = snprintf(response, sizeof(response),
                                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                "<html><body><h1>Calculation Result</h1>"
                                "<p>%d + %d = %d</p></body></html>",
                                a, b, sum);

    send(client_socket, response, response_len, 0);
}

int parse_query(const char *query, const char *key) {
    char *pos = strstr(query, key);
    if (!pos) return 0;

    pos += strlen(key) + 1;
    return atoi(pos);
}