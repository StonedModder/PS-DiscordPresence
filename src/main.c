#include "presence.h"
#include "metadata.h"
#include "psdp_notify.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static struct presence_config cfg;
static struct presence_snapshot last;
extern int sceKernelGetProsperoSystemSwVersion(void *);

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len) { ssize_t n = send(fd, p, len, 0); if (n <= 0) return -1; p += n; len -= (size_t)n; }
    return 0;
}

static void reply(int fd, int status, const char *type, const char *body) {
    char header[256];
    int n = snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status, status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request", type, strlen(body));
    (void)send_all(fd, header, (size_t)n); (void)send_all(fd, body, strlen(body));
}

typedef struct {
    uint64_t size;
    char str_version[0x1c];
    uint32_t bin_version;
    uint64_t reserved;
} sce_sw_version_t;

/* Live system software version, e.g. 0x09600000 for 9.60. The proc-param SDK
   version (kernel_get_fw_version) is what libkernel was built against and can
   lag the installed firmware, so it is only a fallback. */
static uint32_t firmware_version(void) {
    static uint32_t cached;
    if (cached) return cached;
    sce_sw_version_t v; memset(&v, 0, sizeof(v)); v.size = sizeof(v);
    if (sceKernelGetProsperoSystemSwVersion(&v) == 0 && v.bin_version) cached = v.bin_version;
    else cached = kernel_get_fw_version();
    return cached;
}

static int bounded_state_query(struct fg_state_response *out, unsigned timeout_seconds) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t child = fork();
    if (child == 0) {
        close(pipefd[0]);
        struct fg_state_response state;
        int rc = fg_state_query(&state);
        (void)write(pipefd[1], &rc, sizeof(rc));
        if (rc == 0) (void)write(pipefd[1], &state, sizeof(state));
        close(pipefd[1]);
        _exit(0);
    }
    close(pipefd[1]);
    if (child < 0) { close(pipefd[0]); return -1; }
    fd_set set; FD_ZERO(&set); FD_SET(pipefd[0], &set);
    struct timeval tv = { .tv_sec = (time_t)timeout_seconds, .tv_usec = 0 };
    int ready = select(pipefd[0] + 1, &set, NULL, NULL, &tv);
    int rc = -1;
    if (ready == 1 && read(pipefd[0], &rc, sizeof(rc)) == (ssize_t)sizeof(rc) && rc == 0 &&
        read(pipefd[0], out, sizeof(*out)) == (ssize_t)sizeof(*out)) {
        close(pipefd[0]); (void)waitpid(child, NULL, 0); return 0;
    }
    close(pipefd[0]);
    kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
    return ready == 0 ? -ETIMEDOUT : -1;
}

static int load_config(void) {
    int fd = open(PS_DISCORD_PRESENCE_CONFIG_PATH, O_RDONLY);
    char buf[16385];
    if (fd < 0) { presence_config_defaults(&cfg); return 0; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1); close(fd);
    if (n <= 0) { presence_config_defaults(&cfg); return -1; }
    buf[n] = 0; return presence_config_parse(buf, (size_t)n, &cfg);
}

static int save_config(const char *body, size_t length) {
    char serialized[512];
    if (presence_config_parse(body, length, &cfg) || presence_config_serialize(&cfg, serialized, sizeof(serialized))) return -1;
    mkdir(PS_DISCORD_PRESENCE_DATA_DIR, 0700);
    int fd = open(PS_DISCORD_PRESENCE_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    int ok = write(fd, serialized, strlen(serialized)) == (ssize_t)strlen(serialized);
    close(fd); return ok ? 0 : -1;
}

static void client(int fd) {
    char request[17408], *body, *line;
    ssize_t n = recv(fd, request, sizeof(request) - 1, 0);
    if (n <= 0) return;
    request[n] = 0;
    body = strstr(request, "\r\n\r\n");
    if (!body) { reply(fd, 400, "text/plain", "bad request"); return; }
    *body = 0; body += 4;
    line = strtok(request, "\r\n");
    if (!line) { reply(fd, 400, "text/plain", "bad request"); return; }
    if (!strcmp(line, "GET /api/status HTTP/1.1")) {
        struct fg_state_response state;
        struct presence_metadata meta;
        char output[2048], title[400], reason[340];
        uint32_t firmware = firmware_version();
        char firmware_text[16] = "";
        if (firmware) snprintf(firmware_text, sizeof(firmware_text), "%x.%02x", (firmware >> 24) & 0xff, (firmware >> 16) & 0xff);
        int query_rc = bounded_state_query(&state, 3);
        if (query_rc != 0) {
            snprintf(output, sizeof(output), "{\"state\":\"probe_timeout\",\"probe_rc\":%d,\"firmware\":\"%s\",\"reason\":\"game-state probe exceeded 3 seconds; retry after ShellUI settles\"}\n", query_rc, firmware_text);
            reply(fd, 200, "application/json", output); return;
        }
        presence_stabilize(&last, &state);
        presence_reduce(&last, &state, &last);
        presence_metadata_resolve(state.titleid, &meta);
        presence_json_escape(meta.title_name, title, sizeof(title));
        presence_json_escape(state.reason, reason, sizeof(reason));
        snprintf(output, sizeof(output), "{\"state\":\"%s\",\"confidence\":%u,\"firmware\":\"%s\",\"titleid\":\"%s\",\"titleName\":\"%s\",\"iconUrl\":\"/api/icon?titleid=%s\",\"contentid\":\"%s\",\"reason\":\"%s\"}\n", presence_class_name(state.classification), state.confidence, firmware_text, cfg.show_title_id ? state.titleid : "", title, meta.icon_path[0] ? state.titleid : "", cfg.show_content_id ? state.contentid : "", reason);
        reply(fd, 200, "application/json", output); return;
    }
    if (!strcmp(line, "GET /api/config HTTP/1.1")) {
        char output[512]; presence_config_serialize(&cfg, output, sizeof(output)); reply(fd, 200, "application/json", output); return;
    }
    if (!strcmp(line, "POST /api/config HTTP/1.1")) {
        if (save_config(body, strlen(body))) reply(fd, 400, "application/json", "{\"error\":\"invalid config\"}\n");
        else reply(fd, 200, "application/json", "{\"ok\":true}\n");
        return;
    }
    reply(fd, 404, "text/plain", "not found");
}

int main(void) {
    uint8_t caps[16]; memset(caps, 0xff, sizeof(caps));
    kernel_set_ucred_authid(getpid(), 0x3800000000010003l);
    kernel_set_ucred_caps(getpid(), caps);
    mkdir(PS_DISCORD_PRESENCE_DATA_DIR, 0700);
    (void)load_config();
    if (daemon(0, 0)) return 1;
    int listener = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (listener < 0) return 2;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address; memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(PS_DISCORD_PRESENCE_PORT);
    if (bind(listener, (void *)&address, sizeof(address)) || listen(listener, 4)) return 3;
    klog_printf("PS-DiscordPresence listening port=%u probe_timeout=3s", PS_DISCORD_PRESENCE_PORT);
    psdp_notify("PS-DiscordPresence enabled — local UI :%u", PS_DISCORD_PRESENCE_PORT);
    for (;;) { int client_fd = accept(listener, NULL, NULL); if (client_fd >= 0) { client(client_fd); close(client_fd); } }
}
