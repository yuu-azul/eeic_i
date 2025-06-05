#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h> // waitpid() のため
#include <signal.h>   // signal() のため
#define BUFFER_SIZE 1024
volatile sig_atomic_t terminate_flag = 0;
void sigint_handler(int sig) {
    terminate_flag = 1;
    fprintf(stderr, "\n終了シグナル受信。プログラムを終了します...\n");
}
// エラー処理関数
void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}
// 送信プロセス関数
void sender_process(const char *peer_ip, int peer_port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    int n;
    fprintf(stderr, "[送信プロセス] 開始。接続先: %s:%d\n", peer_ip, peer_port);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_exit("[送信プロセス] ソケット作成エラー");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(peer_port);
    if (inet_pton(AF_INET, peer_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "[送信プロセス] 無効なIPアドレス形式です\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // 接続リトライ処理
    int retry_count = 0;
    const int MAX_RETRIES = 10; // 最大リトライ回数
    const int RETRY_DELAY_SEC = 3;  // リトライ間隔（秒）
    while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        if (terminate_flag) {
             fprintf(stderr, "[送信プロセス] 接続試行中に終了シグナル受信。\n");
             close(sockfd);
             exit(EXIT_FAILURE);
        }
        retry_count++;
        if (retry_count >= MAX_RETRIES) {
            perror("[送信プロセス] 接続エラー (最大リトライ回数超過)");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "[送信プロセス] 接続リトライ中 (%d/%d)... %d秒後に再試行します。\n", retry_count, MAX_RETRIES, RETRY_DELAY_SEC);
        sleep(RETRY_DELAY_SEC);
    }
    fprintf(stderr, "[送信プロセス] サーバー (%s:%d) に接続成功。\n", peer_ip, peer_port);
    while (!terminate_flag && (n = read(STDIN_FILENO, buffer, BUFFER_SIZE)) > 0) {
        if (write(sockfd, buffer, n) < 0) {
            if (!terminate_flag) perror("[送信プロセス] 書き込みエラー");
            break;
        }
    }
    if (n < 0 && !terminate_flag) {
        perror("[送信プロセス] 標準入力からの読み込みエラー");
    }
    if (n == 0) {
         fprintf(stderr, "[送信プロセス] 標準入力EOF。送信を終了します。\n");
    }
    fprintf(stderr, "[送信プロセス] 終了。\n");
    close(sockfd);
}
// 受信プロセス関数
void receiver_process(int my_port) {
    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    fprintf(stderr, "[受信プロセス] 開始。待受ポート: %d\n", my_port);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_exit("[受信プロセス] ソケット作成エラー");
    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("[受信プロセス] setsockopt(SO_REUSEADDR) 失敗");
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(my_port);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        error_exit("[受信プロセス] バインドエラー");
    }
    if (listen(sockfd, 1) < 0) error_exit("[受信プロセス] リッスンエラー"); // 1接続のみを想定
    fprintf(stderr, "[受信プロセス] ポート %d で接続待機中...\n", my_port);
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0) {
        if (terminate_flag) {
            fprintf(stderr, "[受信プロセス] accept中に終了シグナル受信。\n");
        } else {
            perror("[受信プロセス] acceptエラー");
        }
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(cli_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
    fprintf(stderr, "[受信プロセス] クライアント %s から接続されました。\n", client_ip_str);
    while (!terminate_flag && (n = read(newsockfd, buffer, BUFFER_SIZE)) > 0) {
        if (write(STDOUT_FILENO, buffer, n) < 0) {
            if (!terminate_flag) perror("[受信プロセス] 標準出力への書き込みエラー");
            break;
        }
    }
    if (n < 0 && !terminate_flag) {
         perror("[受信プロセス] ソケットからの読み込みエラー");
    }
    if (n == 0) {
        fprintf(stderr, "[受信プロセス] 相手が接続を閉じました。受信を終了します。\n");
    }
    fprintf(stderr, "[受信プロセス] 終了。\n");
    close(newsockfd);
    close(sockfd);
}
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "使用法: %s <相手ホストIP> <自分の待受ポート> <相手の待受ポート>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *peer_ip = argv[1];
    int my_port = atoi(argv[2]);
    int peer_port = atoi(argv[3]);
    if (my_port <= 0 || my_port > 65535 || peer_port <= 0 || peer_port > 65535) {
        fprintf(stderr, "エラー: 無効なポート番号です。\n");
        exit(EXIT_FAILURE);
    }
    // SIGINT (Ctrl+C) のハンドラを設定
    signal(SIGINT, sigint_handler);
    // SIGPIPE を無視 (書き込み先のソケットが閉じている場合にプロセスが終了するのを防ぐ)
    signal(SIGPIPE, SIG_IGN);
    pid_t pid = fork();
    if (pid < 0) {
        error_exit("fork失敗");
    }
    if (pid == 0) { // 子プロセス: 送信
        // 子プロセスでもシグナルハンドラを再設定することが推奨される場合がある
        // signal(SIGINT, sigint_handler);
        // signal(SIGPIPE, SIG_IGN);
        sender_process(peer_ip, peer_port);
        exit(EXIT_SUCCESS);
    } else { // 親プロセス: 受信
        receiver_process(my_port);
        fprintf(stderr, "[メインプロセス] 受信プロセス終了。送信プロセスの終了を待ちます...\n");
        int status;
        // waitpidで子プロセスの終了を待つ
        // もし受信プロセスが先に終了した場合（例：相手が接続を切断）、
        // 送信プロセスも適切に終了するようにする（例：標準入力EOF、またはシグナル）
        if (waitpid(pid, &status, 0) == -1 && !terminate_flag) { // pid は子プロセスのID
             perror("[メインプロセス] waitpid エラー");
        }
        if (WIFEXITED(status)) {
            fprintf(stderr, "[メインプロセス] 送信プロセスがステータス %d で正常終了しました。\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "[メインプロセス] 送信プロセスがシグナル %d で終了しました。\n", WTERMSIG(status));
        }
         fprintf(stderr, "[メインプロセス] 全プロセス終了。\n");
    }
    return 0;
}