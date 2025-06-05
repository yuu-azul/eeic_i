// rec -t raw -b 16 -c 1 -e s -r 44100 - | ./client_tele 10.0.0.29 12345 | play -t raw -b 16 -c 1 -e s -r 44100 -

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_SIZE 1024 * 64

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

    // エラー処理
    if (args->s < 0) {
        perror("accept");
        close_socket(&args->ss);
        args->end = 1; // Set error flag
        return NULL;
    }
    return NULL;
}

void *send_thread(void *arg){
    char data[BUFFER_SIZE];

    thread_args_t *args = (thread_args_t *)arg;

    while(!args->end){
        if (fgets(data, sizeof(data)-1, stdin) == NULL && !args->end) {
            break;
        } else if (args->s < 0) {
            continue;
        }
        while(1){
            int n = send(args->s, data, strlen(data), 0);
            if (n < 0){
                perror("send");
                close_socket(&args->s);
                close_socket(&args->ss);
                args->end = 1;
                return NULL;
            // } else if (n == 0) {
            //     printf("[exit send_thread]Connection closed by server\n");
            //     args->end = 1;
            //     break;
            } else if (n == strlen(data)) {
                break;
            } else {
                memmove(data, data + n, strlen(data) - n + 1); // Move remaining data to the front
            }
        }
    }
    shutdown(args->s, SHUT_WR);
    return NULL;
}

void *recv_thread(void *arg){
    thread_args_t *args = (thread_args_t *)arg;
    char buffer[BUFFER_SIZE];
    while(1){
        if (args->s < 0){
            continue;
        }
        ssize_t n = recv(args->s, buffer, sizeof(buffer) - 1, 0);
        if (n < 0) {
            perror("recv");
            close_socket(&args->s);
            close_socket(&args->ss);
            printf("[exit recv_thread]Error receiving data.\n");
            args->end = 1;
            return NULL;
        // } else if (n == 0) {
        //     printf("[exit recv_thread]Connection closed by server.\n");
        //     return NULL;
        } else {
            buffer[n] = '\0';
            if (args->end) {
                printf("[exit recv_thread]Server is shutting down.\n");
                return NULL;
            }
            printf("%s", buffer);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]){
    if (argc != 3){ // Changed to accept IP and Port
        fprintf(stderr, "Usage: %s <Server IP> <Port>\n", argv[0]);
        return 1;
    }

    thread_args_t args;
    args.end = 0;
    args.s = -1;
    args.ss = -1; // Not used by client, initialize to -1

    args.s = socket(AF_INET, SOCK_STREAM, 0);
    if (args.s < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close_socket(&args.s);
        return 1;
    }
    if (connect(args.s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close_socket(&args.s);
        return 1;
    }

    pthread_t send_tid, revc_tid;

    if (pthread_create(&send_tid, NULL, send_thread, &args) != 0){
        perror("pthread_create send_thread");
        close_socket(&args.s);
        return 1;
    }
    if (pthread_create(&revc_tid, NULL, recv_thread, &args) != 0){
        perror("pthread_create recv_thread");
        close_socket(&args.s);
        pthread_cancel(send_tid); // Attempt to cancel, or use a more graceful shutdown
        pthread_join(send_tid, NULL);
        return 1;
    }

    if (pthread_join(send_tid, NULL) != 0){
        perror("pthread_join send_tid");
        close_socket(&args.s);
        pthread_cancel(revc_tid);
        pthread_join(revc_tid, NULL);
        return 1;
    }
    if (pthread_join(revc_tid, NULL) != 0){
        perror("pthread_join recv_tid");
        close_socket(&args.s);
        return 1;
    }

    close_socket(&args.s);
    close_socket(&args.ss);
    return 0;
}