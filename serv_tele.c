// rec -t raw -b 16 -c 1 -e s -r 44100 - | ./serv_tele 12345 | play -t raw -b 16 -c 1 -e s -r 44100 -

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
    data[BUFFER_SIZE - 1] = '\0';

    thread_args_t *args = (thread_args_t *)arg;

    while(!args->end){
        if (fgets(data, sizeof(data)-1, stdin) == NULL && !args->end) {
            break;
        } else if (args->s < 0) {
            // printf("No client connected, waiting for connection...\n");
            continue;
        }

        while(1){
            int n = send(args->s, data, strlen(data), 0);
            if (n < 0){
                perror("send");
                close_socket(&args->s);
                close_socket(&args->ss);
                printf("[exit send_thread]Error sending data.\n");
                args->end = 1;
                return NULL;
            // } else if (n == 0) {
            //     printf("[exit send_thread]Connection closed by client.\n");
            //     args->end = 1;
            //     break;
            } else if (n == strlen(data)) {
                break;
            } else {
                memmove(data, data + n, strlen(data) - n + 1);
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
        //     printf("[exit recv_thread]Connection closed by client.\n");
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
    if (argc != 2){
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }

    thread_args_t args;
    args.end = 0;
    args.s = -1; // Initialize socket descriptor
    args.ss = socket(AF_INET, SOCK_STREAM, 0);


    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(args.ss, (struct sockaddr *)&addr, sizeof(addr)) < 0){perror("bind"); close_socket(&args.ss); return 1;}

    if(listen(args.ss, 5)){perror("listen"); close_socket(&args.ss); return 1;}

    // printf("Server is listening on port %s\n", argv[1]);
    
    pthread_t accept_tid, send_tid, revc_tid;
    if (pthread_create(&accept_tid, NULL, accept_thread, &args) != 0){
        perror("pthread_create");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }
    if (pthread_create(&send_tid, NULL, send_thread, &args) != 0){
        perror("pthread_create");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }if (pthread_create(&revc_tid, NULL, recv_thread, &args) != 0){
        perror("pthread_create");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }


    if (pthread_join(accept_tid, NULL) != 0){
        perror("pthread_join");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }
    if (pthread_join(send_tid, NULL) != 0){
        perror("pthread_join");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }
    if (pthread_join(revc_tid, NULL) != 0){
        perror("pthread_join");
        close_socket(&args.ss);
        close_socket(&args.s);
        return 1;
    }

    close_socket(&args.s);
    close_socket(&args.ss);
    return 0;
}