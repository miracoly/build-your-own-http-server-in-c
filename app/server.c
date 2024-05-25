#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

typedef enum {
    GET,
    POST,
    HTTP_METHOD_NUM
} http_method;

char const* const method_to_string[HTTP_METHOD_NUM] = {
        [GET] = "GET",
        [POST] = "POST",
};

http_method to_method(const char method[static 1]) {
    if (strcmp(method, "GET") == 0) return GET;
    else if (strcmp(method, "POST") == 0) return POST;
    else return -1;
}

typedef struct {
    int method;
    char* path;
} http_request;

typedef struct {
    char* key;
    char* value;
} http_header;

typedef struct {
    uint16_t status_code;
    size_t headers_len;
    http_header* headers;
    char body[];
} http_response;

static http_request parse_request(size_t len, char bytes[len]) {
    char* method = strtok(bytes, " ");
    char* path = strtok(NULL, " ");
    return (http_request) {
            .method = to_method(method),
            .path = path
    };
}

static void handle_root(int client_socket_fd) {
    const char success_msg[] = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_socket_fd, success_msg, sizeof(success_msg), 0);
}

static void handle_echo(int client_socket_fd, const http_request* request) {
    if (strlen((*request).path) <= 6) {
        const char error_msg[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
    } else {
        const char* echo = (*request).path + 6;
        size_t echo_len = strlen(echo);
        char* msg = calloc(1024, sizeof(char));
        sprintf(msg, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", echo_len, echo);
        send(client_socket_fd, msg, strlen(msg), 0);
    }
}

static void handle_unknown(int client_socket_fd) {
    const char error_msg[] = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(client_socket_fd, error_msg, sizeof(error_msg), 0);
}

static void handle_request(int server_fd, struct sockaddr_in client_addr[1]) {
    printf("Client connected\n");
    socklen_t client_addr_len = sizeof(*client_addr);
    int client_socket_fd = accept(server_fd, (struct sockaddr*) client_addr, &client_addr_len);
    char request_bytes[1024] = {0};
    const ssize_t read_n = read(client_socket_fd, request_bytes, sizeof(request_bytes));
    if (read_n <= 1) {
        const char error_msg[] = "HTTP/1.1 500 OK\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
        return;
    }

    const http_request request = parse_request(sizeof(request_bytes), request_bytes);

    if (strcmp(request.path, "/") == 0)
        handle_root(client_socket_fd);
    else if (strncmp(request.path, "/echo/", 6) == 0)
        handle_echo(client_socket_fd, &request);
    else
        handle_unknown(client_socket_fd);
}

int main(void) {
    // Disable output buffering
    setbuf(stdout, NULL);

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    printf("Logs from your program will appear here!\n");

    int server_fd;
    struct sockaddr_in client_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        printf("SO_REUSEPORT failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {.sin_family = AF_INET,
            .sin_port = htons(4221),
            .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) != 0) {
        printf("Bind failed: %s \n", strerror(errno));
        return EXIT_FAILURE;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Waiting for a client to connect...\n");

    handle_request(server_fd, &client_addr);

    close(server_fd);

    return EXIT_SUCCESS;
}
