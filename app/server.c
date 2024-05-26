#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <threads.h>

typedef enum {
    GET,
    POST,
    HTTP_METHOD_NUM
} http_method;

char const* const method_to_string[HTTP_METHOD_NUM] = {
        [GET] = "GET",
        [POST] = "POST",
};

typedef enum {
    OK = 200,
    BAD_REQUEST = 400,
    NOT_FOUND = 404,
    INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NUM
} http_status;

char const* const status_to_string[HTTP_STATUS_NUM] = {
        [OK] = "OK",
        [BAD_REQUEST] = "Bad Request",
        [NOT_FOUND] = "Not Found",
        [INTERNAL_SERVER_ERROR] = "Internal Server Error",
};

http_method to_method(const char method[static 1]) {
    if (strcmp(method, "GET") == 0) return GET;
    else if (strcmp(method, "POST") == 0) return POST;
    else return -1;
}

typedef struct {
    const char* key;
    const char* value;
} http_header;

typedef struct {
    int method;
    char* path;
    size_t headers_len;
    http_header headers[];
} http_request;

typedef struct {
    http_status status_code;
    char body[512];
    size_t headers_len;
    http_header headers[];
} http_response;

http_response* response_create(http_status status) {
    http_response* response = calloc(1, sizeof(http_response));
    response->status_code = status;
    return response;
}

http_response* response_add_body(http_response* response, const char body[static 1]) {
    if (!response) return NULL;
    strncpy(response->body, body, 511);
    return response;
}

http_response*
response_add_header(http_response* response, const char key[static 1], const char value[static 1]) {
    if (!response) return NULL;
    response = realloc(response,
                       sizeof(http_response) + (response->headers_len + 1) * sizeof(http_header));
    if (!response) return NULL;

    response->headers[response->headers_len++] = (http_header) {
            .key = key,
            .value = value
    };
    return response;
}

char* response_serialize(http_response* response) {
    if (!response) return NULL;

    char* serialized = calloc(1024, sizeof(char));
    sprintf(serialized, "HTTP/1.1 %d %s\r\n", response->status_code,
            status_to_string[response->status_code]);
    for (size_t i = 0; i < response->headers_len; i++) {
        sprintf(serialized + strlen(serialized), "%s: %s\r\n", response->headers[i].key,
                response->headers[i].value);
    }
    sprintf(serialized + strlen(serialized), "\r\n%s", response->body);
    return serialized;
}

void response_destroy(http_response* response) {
    if (!response) return;
    free(response);
}

static http_request* parse_request(size_t len, char bytes[len]) {
    http_request* request = calloc(1, sizeof(http_request));

    request->method = to_method(strtok(bytes, " "));
    request->path = strtok(NULL, " ");
    strtok(NULL, "\r");
    char* token = strtok(NULL, ":") + 1;
    while (token) {
        request = realloc(request,
                          sizeof(http_request) + (request->headers_len + 1) * sizeof(http_header));
        char* key = token;
        char* pre_val = strtok(NULL, "\r");
        if (!pre_val) break;
        char* value = pre_val + 1;
        request->headers[request->headers_len++] = (http_header) {
                .key = key,
                .value = value
        };
        token = strtok(NULL, ":") + 1;
    }

    return request;
}

static const char* get_header(const http_request* request, const char key[static 1]) {
    for (size_t i = 0; i < request->headers_len; i++) {
        if (strcmp(request->headers[i].key, key) == 0) {
            return request->headers[i].value;
        }
    }
    return NULL;
}

static void handle_root(int client_socket_fd) {
    const char success_msg[] = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_socket_fd, success_msg, sizeof(success_msg), 0);
}

static void handle_echo(int client_socket_fd, const http_request* request) {
    if (!request) return;

    if (strlen((*request).path) <= 6) {
        const char error_msg[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
    } else {
        const char* echo = request->path + 6;
        const size_t echo_len = strlen(echo);
        char echo_len_str[20];
        sprintf(echo_len_str, "%zu", echo_len);
        http_response* response = response_create(200);
        response_add_header(response, "Content-Type", "text/plain");
        response_add_header(response, "Content-Length", echo_len_str);
        response_add_body(response, echo);
        char* serialized = response_serialize(response);
        printf("Echoing: %s\n", echo);
        send(client_socket_fd, serialized, strlen(serialized), 0);
        response_destroy(response);
    }
}

static void
handle_files(int client_socket_fd, const http_request* request, const char directory[100]) {
    if (!request) return;
    printf("File Directory: %s\n", directory);

    if (strlen((*request).path) <= 7) {
        const char error_msg[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
    } else {
        const char* file_name = request->path + 7;
        char file_path[200] = {0};
        sprintf(file_path, "%s/%s", directory, file_name);
        FILE* file = fopen(file_path, "r");
        if (!file) {
            http_response* response = response_create(404);
            response_add_header(response, "Content-Type", "application/octet-stream");
            const char* serialized = response_serialize(response);
            send(client_socket_fd, serialized, strlen(serialized), 0);
        } else {
            http_response* response = response_create(200);
            response_add_header(response, "Content-Type", "application/octet-stream");
            char body[1024] = {0};
            const size_t body_len = fread(body, 1, sizeof(body), file);
            char body_len_str[20];
            sprintf(body_len_str, "%zu", body_len);
            fclose(file);
            response_add_header(response, "Content-Length", body_len_str);
            response_add_body(response, body);
            char* serialized = response_serialize(response);
            printf("File Content: %s\n", body);
            send(client_socket_fd, serialized, strlen(serialized), 0);
            response_destroy(response);
        }
    }
}

static void handle_user_agent(int client_socket_fd, const http_request* request) {
    if (!request) return;

    const char* user_agent = get_header(request, "User-Agent");

    if (!user_agent || strlen(request->path) > 11) {
        const char error_msg[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
    } else {
        const size_t user_agent_len = strlen(user_agent);
        char user_agent_len_str[20] = {0};
        sprintf(user_agent_len_str, "%zu", user_agent_len);
        http_response* response = response_create(OK);
        response_add_header(response, "Content-Type", "text/plain");
        response_add_header(response, "Content-Length", user_agent_len_str);
        response_add_body(response, user_agent);
        const char* serialized = response_serialize(response);
        send(client_socket_fd, serialized, strlen(serialized), 0);
        response_destroy(response);
    }
}

static void handle_unknown(int client_socket_fd) {
    const char error_msg[] = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(client_socket_fd, error_msg, sizeof(error_msg), 0);
}

static void handle_request(int client_socket_fd, const char directory[100]) {
    printf("Client connected\n");
    char request_bytes[1024] = {0};
    const ssize_t read_n = read(client_socket_fd, request_bytes, sizeof(request_bytes));
    if (read_n <= 1) {
        const char error_msg[] = "HTTP/1.1 500 OK\r\n\r\n";
        send(client_socket_fd, error_msg, sizeof(error_msg), 0);
        return;
    }

    http_request* request = parse_request(sizeof(request_bytes), request_bytes);

    if (strcmp(request->path, "/") == 0)
        handle_root(client_socket_fd);
    else if (strncmp(request->path, "/echo/", 6) == 0)
        handle_echo(client_socket_fd, request);
    else if (strncmp(request->path, "/user-agent", 11) == 0)
        handle_user_agent(client_socket_fd, request);
    else if (strncmp(request->path, "/files/", 7) == 0)
        handle_files(client_socket_fd, request, directory);
    else
        handle_unknown(client_socket_fd);

    free(request);
}

typedef struct {
    int client_socket_fd;
    char directory[100];
} handle_concurrently_args;

static int handle_concurrently(void* args) {
    handle_concurrently_args* handle_args = (handle_concurrently_args*) args;
    handle_request(handle_args->client_socket_fd, handle_args->directory);
    close(handle_args->client_socket_fd);
    free(handle_args);
    return 0;
}

int main(int argc, char* argv[]) {
    char directory[100] = {0};
    if (argc > 1) {
        strncpy(directory, argv[2], sizeof(directory));
        directory[sizeof(directory) - 1] = '\0';
        printf("Directory: %s\n", directory);
    }
    // Disable output buffering
    setbuf(stdout, NULL);

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    printf("Logs from your program will appear here!\n");

    int server_fd;

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

    while (1) {
        thrd_t thread;
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_socket_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
        handle_concurrently_args* args = calloc(1, sizeof(handle_concurrently_args));
        args->client_socket_fd = client_socket_fd;
        strncpy(args->directory, directory, sizeof(directory));
        args->directory[sizeof(directory) - 1] = '\0';
        thrd_create(&thread, handle_concurrently, args);
        thrd_detach(thread);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
