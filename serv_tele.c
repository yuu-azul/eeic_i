// rec -t raw -b 16 -c 1 -e s -r 44100 - | ./serv_tele 12345 | play -t raw -b 16 -c 1 -e s -r 44100 -

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

typedef struct {
    int s;
    int ss;
    int end;
} thread_args_t;

void close_socket(int *s) {
    if (*s >= 0) {
        close(*s);
        *s = -1;
    }
}

void *accept_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    args->s = accept(args->ss, (struct sockaddr *)&client_addr, &client_len);
    if (args->s < 0) {
        perror("accept");
        args->end = 1;
    }
    return NULL;
}

void *send_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char buffer[BUFFER_SIZE];
    while (!args->end) {
        ssize_t bytes_read = read(STDIN_FILENO, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            args->end = 1;
            break;
        }

        if (args->s < 0) {
            // クライアント接続待ち中は入力を捨てる
            continue;
        }

        ssize_t total_sent = 0;
        while (total_sent < bytes_read) {
            ssize_t sent = send(args->s, buffer + total_sent, bytes_read - total_sent, 0);
            if (sent <= 0) {
                perror("send");
                args->end = 1;
                return NULL;
            }
            total_sent += sent;
        }
    }
    if (args->s >= 0) shutdown(args->s, SHUT_WR);
    return NULL;
}

void *recv_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char buffer[BUFFER_SIZE];
    while (!args->end) {
        if (args->s < 0) continue;

        ssize_t n = recv(args->s, buffer, sizeof(buffer), 0);
        if (n < 0) {
            perror("recv");
            args->end = 1;
            break;
        } else if (n == 0) {
            // 接続終了
            args->end = 1;
            break;
        }

        ssize_t total_written = 0;
        while (total_written < n) {
            ssize_t written = write(STDOUT_FILENO, buffer + total_written, n - total_written);
            if (written <= 0) {
                perror("write");
                args->end = 1;
                return NULL;
            }
            total_written += written;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }

    thread_args_t args;
    args.end = 0;
    args.s = -1;
    args.ss = socket(AF_INET, SOCK_STREAM, 0);
    if (args.ss < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(argv[1])),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(args.ss, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close_socket(&args.ss);
        return 1;
    }

    if (listen(args.ss, 1) < 0) {
        perror("listen");
        close_socket(&args.ss);
        return 1;
    }

    pthread_t accept_tid, send_tid, recv_tid;
    pthread_create(&accept_tid, NULL, accept_thread, &args);
    pthread_create(&send_tid, NULL, send_thread, &args);
    pthread_create(&recv_tid, NULL, recv_thread, &args);

    pthread_join(accept_tid, NULL);
    pthread_join(send_tid, NULL);
    pthread_join(recv_tid, NULL);

    close_socket(&args.s);
    close_socket(&args.ss);
    return 0;
}
