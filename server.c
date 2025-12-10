// server.c
// Final optimized multi-client chat server (threads). Safe string ops, logging, graceful shutdown.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#define PORT 9090
#define MAX_CLIENTS 128
#define BUF_SIZE 4096
#define NAME_LEN 32
#define BACKLOG 16

typedef struct {
    int fd;
    int alive;
    int64_t id;
    pthread_t thread;
    char name[NAME_LEN];
} client_t;

static client_t clients[MAX_CLIENTS];
static pthread_mutex_t clients_mtx = PTHREAD_MUTEX_INITIALIZER;
static int64_t next_id = 1;
static int listen_fd = -1;
static FILE *logf = NULL;

/* Timestamp helper */
static void get_timestamp(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%F %T", &tm);
}

/* Logger (varargs) */
static void log_event(const char *fmt, ...) {
    if (!logf) return;
    char ts[32];
    get_timestamp(ts, sizeof(ts));
    fprintf(logf, "%s  ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fprintf(logf, "\n");
    fflush(logf);
}

/* send all bytes */
static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* send C-string safely */
static void send_str(int fd, const char *s) {
    if (s) send_all(fd, s, strlen(s));
}

/* find free slot */
static int find_free_slot(void) {
    for (int i = 0; i < MAX_CLIENTS; ++i) if (!clients[i].alive) return i;
    return -1;
}

/* add client to slot, assign name Client-<id> */
static int add_client(int fd) {
    pthread_mutex_lock(&clients_mtx);
    int slot = find_free_slot();
    if (slot >= 0) {
        clients[slot].fd = fd;
        clients[slot].alive = 1;
        clients[slot].id = next_id++;
        /* safe formatting into fixed buffer */
        snprintf(clients[slot].name, NAME_LEN, "Client-%" PRId64, clients[slot].id);
    }
    pthread_mutex_unlock(&clients_mtx);
    return slot;
}

/* remove client */
static void remove_client(int slot) {
    pthread_mutex_lock(&clients_mtx);
    if (slot >= 0 && slot < MAX_CLIENTS && clients[slot].alive) {
        close(clients[slot].fd);
        clients[slot].alive = 0;
        clients[slot].name[0] = '\0';
        clients[slot].id = 0;
    }
    pthread_mutex_unlock(&clients_mtx);
}

/* find client by id */
static client_t *find_by_id(int64_t id) {
    client_t *res = NULL;
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].alive && clients[i].id == id) { res = &clients[i]; break; }
    }
    pthread_mutex_unlock(&clients_mtx);
    return res;
}

/* broadcast to all except except_fd (-1 = none) */
static void broadcast_except(const char *msg, int except_fd) {
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].alive && clients[i].fd != except_fd) send_str(clients[i].fd, msg);
    }
    pthread_mutex_unlock(&clients_mtx);
}

/* list users to a fd */
static void list_users(int fd) {
    char out[4096];
    size_t pos = 0;
    pos += snprintf(out + pos, sizeof(out) - pos, "=== Connected Users ===\n");
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < MAX_CLIENTS && pos + 64 < sizeof(out); ++i) {
        if (clients[i].alive) pos += snprintf(out + pos, sizeof(out) - pos, "ID:%" PRId64 "  %s\n", clients[i].id, clients[i].name);
    }
    pthread_mutex_unlock(&clients_mtx);
    pos += snprintf(out + pos, sizeof(out) - pos, "=======================\n");
    send_all(fd, out, strlen(out));
}

/* client thread: arg is pointer to slot (malloc'd) */
static void *client_thread(void *arg) {
    int slot = *(int *)arg;
    free(arg);

    int fd;
    int64_t my_id;
    char my_name[NAME_LEN];

    pthread_mutex_lock(&clients_mtx);
    fd = clients[slot].fd;
    my_id = clients[slot].id;
    snprintf(my_name, NAME_LEN, "%s", clients[slot].name);
    pthread_mutex_unlock(&clients_mtx);

    char buf[BUF_SIZE];

    /* welcome */
    snprintf(buf, sizeof(buf), "Welcome %s (ID:%" PRId64 ")\nCommands: /name <new>, /list, /msg <id> <text>, /quit\n", my_name, my_id);
    send_str(fd, buf);

    /* announce */
    snprintf(buf, sizeof(buf), "[Server] %s (ID:%" PRId64 ") joined.\n", my_name, my_id);
    broadcast_except(buf, fd);
    log_event("CONNECT id=%" PRId64 " name=%s", my_id, my_name);

    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
        if (n == 0) break;

        if (buf[0] == '/') {
            if (strncmp(buf, "/quit", 5) == 0) break;

            if (strncmp(buf, "/name ", 6) == 0) {
                char *newn = buf + 6;
                if (*newn == '\0') { send_str(fd, "Usage: /name <newname>\n"); continue; }

                /* safe bounded copy for name to avoid truncation warnings */
                size_t nl = strnlen(newn, NAME_LEN - 1);
                pthread_mutex_lock(&clients_mtx);
                memcpy(clients[slot].name, newn, nl);
                clients[slot].name[nl] = '\0';
                memcpy(my_name, clients[slot].name, nl + 1);
                pthread_mutex_unlock(&clients_mtx);

                snprintf(buf, sizeof(buf), "[Server] ID %" PRId64 " is now known as %s\n", my_id, my_name);
                broadcast_except(buf, -1);
                log_event("RENAME id=%" PRId64 " name=%s", my_id, my_name);
                continue;
            }

            if (strncmp(buf, "/list", 5) == 0) { list_users(fd); continue; }

            if (strncmp(buf, "/msg ", 5) == 0) {
                char *p = buf + 5;
                int64_t tid = atoll(p);
                while (*p && *p != ' ') p++;
                if (*p == ' ') p++;
                client_t *tgt = find_by_id(tid);
                if (tgt) {
                    char pm[BUF_SIZE + 64];
                    snprintf(pm, sizeof(pm), "[PM from %s (ID:%" PRId64 ")]: %s\n", my_name, my_id, p);
                    send_str(tgt->fd, pm);
                    send_str(fd, "[PM sent]\n");
                    log_event("PM from=%" PRId64 " to=%" PRId64 " text=%s", my_id, tgt->id, p);
                } else { send_str(fd, "User not found.\n"); }
                continue;
            }

            send_str(fd, "Unknown command.\n");
            continue;
        }

        /* normal message -> broadcast */
        char out[BUF_SIZE + 64];
        snprintf(out, sizeof(out), "%s (ID:%" PRId64 "): %s\n", my_name, my_id, buf);
        broadcast_except(out, fd);
        log_event("MSG id=%" PRId64 " name=%s text=%s", my_id, my_name, buf);
    }

    /* disconnect */
    snprintf(buf, sizeof(buf), "[Server] %s (ID:%" PRId64 ") disconnected.\n", my_name, my_id);
    broadcast_except(buf, fd);
    log_event("DISCONNECT id=%" PRId64 " name=%s", my_id, my_name);
    remove_client(slot);
    return NULL;
}

/* shutdown */
static void shutdown_server(void) {
    if (listen_fd != -1) close(listen_fd);
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].alive) {
            send_str(clients[i].fd, "[Server] Shutting down.\n");
            close(clients[i].fd);
            clients[i].alive = 0;
        }
    }
    pthread_mutex_unlock(&clients_mtx);
    if (logf) { log_event("SERVER SHUTDOWN"); fclose(logf); }
}

static void sig_handler(int s) { (void)s; shutdown_server(); exit(0); }

/* main */
int main(void) {
    memset(clients, 0, sizeof(clients));
    logf = fopen("server.log", "a");

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); exit(1); }

    printf("Chat server running on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli, &len);
        if (client_fd < 0) {
            if (errno == EINTR) break;
            perror("accept");
            continue;
        }

        int slot = add_client(client_fd);
        if (slot < 0) { send_str(client_fd, "Server full.\n"); close(client_fd); continue; }

        int *arg = malloc(sizeof(int));
        if (!arg) { perror("malloc"); remove_client(slot); continue; }
        *arg = slot;

        pthread_create(&clients[slot].thread, NULL, client_thread, arg);
        pthread_detach(clients[slot].thread);
    }

    shutdown_server();
    return 0;
}
