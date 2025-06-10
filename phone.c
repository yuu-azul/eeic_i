//修正版
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>   // waitpid() のため
#include <signal.h>     // signal() のため
#include <sys/select.h> // select() のため
#include <errno.h>      // errno のため
#define BUFFER_SIZE 4096 // データ送受信用バッファ
#define DISCARD_BUFFER_SIZE 1024 // 読み捨て用バッファ
volatile sig_atomic_t terminate_flag = 0;
void sigint_handler(int sig) {
    terminate_flag = 1;
    // fprintf(stderr, "\n終了シグナル受信。プログラムを終了します...\n"); // ハンドラ内でのprintfは非推奨の場合あり
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
    char buffer[BUFFER_SIZE]; // 送信用バッファ
    char discard_buffer[DISCARD_BUFFER_SIZE]; // 読み捨て用バッファ
    int n;
    fprintf(stderr, "[送信プロセス] 開始。接続先: %s:%d\n", peer_ip, peer_port);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_exit("[送信プロセス] ソケット作成エラー");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(peer_port);
    if (inet_pton(AF_INET, peer_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "[送信プロセス] 無効なIPアドレス形式です: %s\n", peer_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    int connected = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 10; // 最大リトライ回数
    const int RETRY_DELAY_SEC = 2;  // リトライ間隔（秒） / selectのタイムアウト
    while (!connected && retry_count < MAX_RETRIES && !terminate_flag) {
        if (retry_count > 0) { // 初回はメッセージなし
             fprintf(stderr, "[送信プロセス] 接続リトライ %d/%d...\n", retry_count, MAX_RETRIES);
        }
        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            connected = 1;
            fprintf(stderr, "[送信プロセス] サーバー (%s:%d) に接続成功。\n", peer_ip, peer_port);
            break; // 接続成功なのでリトライループを抜ける
        }
        // connect失敗時の処理
        if (terminate_flag) {
            fprintf(stderr, "[送信プロセス] 接続試行中に終了シグナル受信。\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        // ECONNREFUSED 以外の場合はリトライしない方が良いかもしれないが、ここでは単純化
        // perror("[送信プロセス] connect試行失敗");
        retry_count++;
        if (retry_count >= MAX_RETRIES) {
            perror("[送信プロセス] 接続エラー (最大リトライ回数超過のため終了)");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "[送信プロセス] %d秒待機しつつ標準入力を処理します。\n", RETRY_DELAY_SEC);
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = RETRY_DELAY_SEC;
        timeout.tv_usec = 0;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds); // 標準入力を監視
        int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR && !terminate_flag) { // EINTRはシグナルによる中断なので許容
            perror("[送信プロセス] selectエラー (リトライ中)");
            // 継続困難なエラーなら終了も検討
        }
        if (activity > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            int discarded_bytes = read(STDIN_FILENO, discard_buffer, sizeof(discard_buffer));
            if (discarded_bytes > 0) {
                fprintf(stderr, "[送信プロセス] 待機中に標準入力から %d バイト読み捨てました。\n", discarded_bytes);
            } else if (discarded_bytes == 0) { // 標準入力からEOF
                fprintf(stderr, "[送信プロセス] 待機中に標準入力EOF検知。送信プロセスを終了します。\n");
                close(sockfd);
                exit(EXIT_SUCCESS);
            } else if (!terminate_flag) { // 読み込みエラー (EOFでもない)
                 if (errno != EINTR) perror("[送信プロセス] 待機中の標準入力読み込みエラー");
            }
        }
        // タイムアウト (activity == 0) の場合は何もせず次のリトライへ
        if (terminate_flag) break; // 待機中に終了シグナルを受け取ったらループを抜ける
    }
    if (!connected) {
        fprintf(stderr, "[送信プロセス] 接続できませんでした。送信プロセスを終了します。\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // ----- これ以降は接続後のデータ送受信ループ -----
    // fprintf(stderr, "[送信プロセス] データ送信ループ開始。\n"); // デバッグ用
    long total_sent_bytes = 0;
    while (!terminate_flag && (n = read(STDIN_FILENO, buffer, BUFFER_SIZE)) > 0) {
        // fprintf(stderr, "[送信プロセス] 標準入力から %d バイト読み込みました。\n", n); // デバッグ用
        if (write(sockfd, buffer, n) < 0) {
            if (!terminate_flag && errno != EPIPE) perror("[送信プロセス] 書き込みエラー"); // EPIPEは相手が閉じた場合
            break;
        }
        total_sent_bytes += n;
        // fprintf(stderr, "[送信プロセス] %d バイト送信。総送信: %ld\n", n, total_sent_bytes); // デバッグ用
    }
    if (n < 0 && !terminate_flag && errno != EINTR) {
        perror("[送信プロセス] 標準入力からの読み込みエラー (送信ループ中)");
    }
    if (n == 0) {
         fprintf(stderr, "[送信プロセス] 標準入力EOF。送信を終了します。総送信バイト数: %ld\n", total_sent_bytes);
    }
    fprintf(stderr, "[送信プロセス] 終了。\n");
    shutdown(sockfd, SHUT_WR); // 送信終了を相手に伝える
    close(sockfd);
}
// 受信プロセス関数 (変更なし)
void receiver_process(int my_port) {
    int sockfd_listen, newsockfd_conn; // 変数名を変更して明確化
    socklen_t clilen;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    fprintf(stderr, "[受信プロセス] 開始。待受ポート: %d\n", my_port);
    sockfd_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_listen < 0) error_exit("[受信プロセス] リスニングソケット作成エラー");
    int optval = 1;
    if (setsockopt(sockfd_listen, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("[受信プロセス] setsockopt(SO_REUSEADDR) 失敗");
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(my_port);
    if (bind(sockfd_listen, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // ポートが既に使用されている場合は、少し待ってリトライするなどの処理も考えられる
        error_exit("[受信プロセス] バインドエラー");
    }
    if (listen(sockfd_listen, 1) < 0) error_exit("[受信プロセス] リッスンエラー");
    fprintf(stderr, "[受信プロセス] ポート %d で接続待機中...\n", my_port);
    clilen = sizeof(cli_addr);
    newsockfd_conn = accept(sockfd_listen, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd_conn < 0) {
        if (terminate_flag) {
            fprintf(stderr, "[受信プロセス] accept中に終了シグナル受信。\n");
        } else if (errno != EINTR) { // EINTR以外の場合にエラー表示
            perror("[受信プロセス] acceptエラー");
        }
        close(sockfd_listen);
        exit(EXIT_FAILURE);
    }
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(cli_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
    fprintf(stderr, "[受信プロセス] クライアント %s から接続されました。\n", client_ip_str);
    long total_received_bytes = 0;
    while (!terminate_flag && (n = read(newsockfd_conn, buffer, BUFFER_SIZE)) > 0) {
        // fprintf(stderr, "[受信プロセス] %d バイト受信。\n", n); // デバッグ用
        if (write(STDOUT_FILENO, buffer, n) < 0) {
            if (!terminate_flag && errno != EPIPE) perror("[受信プロセス] 標準出力への書き込みエラー");
            break;
        }
        total_received_bytes += n;
    }
    if (n < 0 && !terminate_flag && errno != EINTR) {
         perror("[受信プロセス] ソケットからの読み込みエラー");
    }
    if (n == 0) {
        fprintf(stderr, "[受信プロセス] 相手が接続を閉じました。受信を終了します。総受信バイト数: %ld\n", total_received_bytes);
    }
    fprintf(stderr, "[受信プロセス] 終了。\n");
    close(newsockfd_conn);
    close(sockfd_listen);
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
        fprintf(stderr, "エラー: 無効なポート番号です。(1-65535の範囲で指定してください)\n");
        exit(EXIT_FAILURE);
    }
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0; // SA_RESTARTは設定しない（システムコールがEINTRで中断されるように）
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction(SIGINT) 失敗");
    }
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN; // SIGPIPEを無視
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1) {
         perror("sigaction(SIGPIPE) 失敗");
    }
    pid_t pid = fork();
    if (pid < 0) {
        error_exit("fork失敗");
    }
    if (pid == 0) { // 子プロセス: 送信
        sender_process(peer_ip, peer_port);
        exit(EXIT_SUCCESS);
    } else { // 親プロセス: 受信
        receiver_process(my_port);
        fprintf(stderr, "[メインプロセス] 受信プロセス終了。送信プロセスの終了を待ちます...\n");
        int status;
        pid_t child_pid_waited = waitpid(pid, &status, 0);
        if (child_pid_waited == -1 && errno != ECHILD && errno != EINTR) { // ECHILDは既に待った後など
             perror("[メインプロセス] waitpid エラー");
        } else if (child_pid_waited > 0) {
            if (WIFEXITED(status)) {
                fprintf(stderr, "[メインプロセス] 送信プロセスがステータス %d で正常終了しました。\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "[メインプロセス] 送信プロセスがシグナル %d (%s) で終了しました。\n",
                        WTERMSIG(status), strsignal(WTERMSIG(status)));
            }
        }
         fprintf(stderr, "[メインプロセス] 全プロセス終了。\n");
    }
    return 0;
}