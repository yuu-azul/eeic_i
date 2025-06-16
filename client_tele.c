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
    int end;
} thread_args_t;

void close_socket(int *s) {
    if (*s >= 0) {
        close(*s);
        *s = -1;
    }
}

void *send_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char data[BUFFER_SIZE];

    while (!args->end) {
        ssize_t bytes_read = read(STDIN_FILENO, data, BUFFER_SIZE);
        if (bytes_read <= 0) {
            if (bytes_read < 0) perror("read (stdin)");
            args->end = 1;
            break;
        }

        // 接続が完了していない場合は読み捨てる
        if (args->s < 0) continue;

        ssize_t total_sent = 0;
        while (total_sent < bytes_read) {
            ssize_t n = send(args->s, data + total_sent, bytes_read - total_sent, 0);
            if (n <= 0) {
                if (n < 0) perror("send");
                args->end = 1;
                return NULL;
            }
            total_sent += n;
        }
    }

    if (args->s >= 0) shutdown(args->s, SHUT_WR);
    return NULL;
}

void *recv_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    char buffer[BUFFER_SIZE];

    while (!args->end) {
        if (args->s < 0) {
            usleep(1000); // busy loop 回避
            continue;
        }

        ssize_t n = recv(args->s, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            if (n < 0) perror("recv");
            args->end = 1;
            return NULL;
        }

        // 音声データをそのまま stdout に出力
        ssize_t total_written = 0;
        while (total_written < n) {
            ssize_t w = write(STDOUT_FILENO, buffer + total_written, n - total_written);
            if (w <= 0) {
                perror("write (stdout)");
                args->end = 1;
                return NULL;
            }
            total_written += w;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3){
        fprintf(stderr, "Usage: %s <Server IP> <Port>\n", argv[0]);
        return 1;
    }

    thread_args_t args;
    args.end = 0;
    args.s = -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close_socket(&sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close_socket(&sock);
        return 1;
    }

    args.s = sock;

    pthread_t send_tid, recv_tid;
    if (pthread_create(&send_tid, NULL, send_thread, &args) != 0) {
        perror("pthread_create send_thread");
        close_socket(&args.s);
        return 1;
    }
    if (pthread_create(&recv_tid, NULL, recv_thread, &args) != 0) {
        perror("pthread_create recv_thread");
        close_socket(&args.s);
        pthread_cancel(send_tid);
        pthread_join(send_tid, NULL);
        return 1;
    }

    pthread_join(send_tid, NULL);
    pthread_join(recv_tid, NULL);

    close_socket(&args.s);
    return 0;
}
