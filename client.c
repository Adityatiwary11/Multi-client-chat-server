# backup current file
cp client.c client.c.bak

# overwrite with the fixed client.c
cat > client.c <<'EOF'
/* client.c
   Threaded client for the multi-client chat server */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 9090
#define BUF_SIZE 4096

static int sockfd = -1;

void *recv_thread(void *arg) {
    (void)arg;   /* silence "unused parameter" warning */

    char buf[BUF_SIZE];
    while (1) {
        ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\\0';
        printf("%s", buf);
        fflush(stdout);
    }
    fprintf(stderr, "\\n[Disconnected from server]\\n");
    exit(0);
    return NULL;
}

void handle_sigint(int sig) {
    (void)sig;
    if (sockfd != -1) {
        const char *q = "/quit\\n";
        send(sockfd, q, strlen(q), 0);
        close(sockfd);
    }
    printf("\\n[Client exiting]\\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\\n", argv[0]);
        return 1;
    }
    const char *server_ip = argv[1];
    struct sockaddr_in serv;

    signal(SIGINT, handle_sigint);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip, &serv.sin_addr) <= 0) { perror("inet_pton"); close(sockfd); return 1; }

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) { perror("connect"); close(sockfd); return 1; }

    printf("✅ Connected to %s:%d\\n", server_ip, PORT);
    printf("Type messages. Commands: /name <new>, /list, /msg <id> <text>, /quit\\n");

    pthread_t rt;
    if (pthread_create(&rt, NULL, recv_thread, NULL) != 0) {
        perror("pthread_create");
        close(sockfd);
        return 1;
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        if (len == 0) continue;
        if (send(sockfd, line, len, 0) <= 0) {
            perror("send");
            break;
        }
        if (strncmp(line, "/quit", 5) == 0) break;
    }

    close(sockfd);
    return 0;
}
EOF

