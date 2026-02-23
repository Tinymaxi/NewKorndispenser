#include "web_server.h"
#include "web_page.h"
#include "dispenser_state.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

static DispenserState* g_state = nullptr;

// ---------- helpers ----------------------------------------------------------

static int parse_int_field(const char* body, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(body, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    return atoi(p);
}

static float parse_float_field(const char* body, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(body, search);
    if (!p) return 0.0f;
    p += strlen(search);
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    return (float)atof(p);
}

static bool parse_string_field(const char* body, const char* key, char* out, int out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && (*p == ':' || *p == ' ' || *p == '\t')) p++;
    if (*p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

// ---------- HTTP response constants ------------------------------------------

static const char HTTP_200_JSON[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Content-Length: ";

static const char HTTP_204[] =
    "HTTP/1.1 204 No Content\r\n"
    "Connection: close\r\n"
    "Access-Control-Allow-Origin: *\r\n\r\n";

static const char HTTP_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n\r\n";

static const char HTTP_OPTIONS[] =
    "HTTP/1.1 204 No Content\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n"
    "Connection: close\r\n\r\n";

// ---------- connection state -------------------------------------------------

#define REQ_BUF_SIZE 1024

// State for streaming large responses (like the HTML page)
struct ConnState {
    // Request buffer (used during receive phase)
    char req_buf[REQ_BUF_SIZE];
    int  req_len;

    // Response streaming (used during send phase)
    const char* send_data;   // Pointer to data being sent (header or body)
    int         send_total;  // Total bytes to send
    int         send_offset; // Bytes already queued

    // For two-part responses (header + body)
    char        resp_header[256];
    int         resp_header_len;
    const char* resp_body;
    int         resp_body_len;
    bool        header_done;  // Header fully queued?
};

// ---------- streaming send with tcp_sent callback ----------------------------

static void cleanup_conn(struct tcp_pcb* pcb, ConnState* cs) {
    if (cs) {
        delete cs;
    }
    tcp_arg(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_close(pcb);
}

static err_t send_more(struct tcp_pcb* pcb, ConnState* cs) {
    // Send header first
    if (!cs->header_done) {
        while (cs->send_offset < cs->resp_header_len) {
            int avail = (int)tcp_sndbuf(pcb);
            if (avail == 0) {
                tcp_output(pcb);
                return ERR_OK;  // Wait for tcp_sent callback
            }
            int chunk = cs->resp_header_len - cs->send_offset;
            if (chunk > avail) chunk = avail;
            err_t err = tcp_write(pcb, cs->resp_header + cs->send_offset, chunk, TCP_WRITE_FLAG_COPY);
            if (err != ERR_OK) {
                cleanup_conn(pcb, cs);
                return ERR_OK;
            }
            cs->send_offset += chunk;
        }
        cs->header_done = true;
        cs->send_offset = 0;
    }

    // Then send body
    while (cs->send_offset < cs->resp_body_len) {
        int avail = (int)tcp_sndbuf(pcb);
        if (avail == 0) {
            tcp_output(pcb);
            return ERR_OK;  // Wait for tcp_sent callback
        }
        int chunk = cs->resp_body_len - cs->send_offset;
        if (chunk > avail) chunk = avail;
        err_t err = tcp_write(pcb, cs->resp_body + cs->send_offset, chunk, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            cleanup_conn(pcb, cs);
            return ERR_OK;
        }
        cs->send_offset += chunk;
    }

    // All data queued — flush and close
    tcp_output(pcb);
    cleanup_conn(pcb, cs);
    return ERR_OK;
}

static err_t tcp_sent_cb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    ConnState* cs = (ConnState*)arg;
    if (!cs) return ERR_OK;
    return send_more(pcb, cs);
}

// ---------- small response helpers -------------------------------------------

static void send_and_close(struct tcp_pcb* pcb, ConnState* cs, const char* data, int len) {
    tcp_write(pcb, data, len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    cleanup_conn(pcb, cs);
}

static void send_json_response(struct tcp_pcb* pcb, ConnState* cs, const char* json, int json_len) {
    // JSON responses are small enough to send in one shot
    char header[256];
    int hlen = snprintf(header, sizeof(header), "%s%d\r\n\r\n", HTTP_200_JSON, json_len);
    tcp_write(pcb, header, hlen, TCP_WRITE_FLAG_COPY);
    tcp_write(pcb, json, json_len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    cleanup_conn(pcb, cs);
}

// Start a streamed large response (header + body via callbacks)
static void start_streaming_response(struct tcp_pcb* pcb, ConnState* cs,
                                      const char* body, int body_len) {
    cs->resp_header_len = snprintf(cs->resp_header, sizeof(cs->resp_header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n\r\n", body_len);
    cs->resp_body = body;
    cs->resp_body_len = body_len;
    cs->header_done = false;
    cs->send_offset = 0;

    // Install the sent callback for streaming
    tcp_sent(pcb, tcp_sent_cb);

    // Start sending
    send_more(pcb, cs);
}

// ---------- route handling ---------------------------------------------------

static void handle_request(struct tcp_pcb* pcb, ConnState* cs) {
    const char* req = cs->req_buf;

    // Parse method and path
    char method[8] = {};
    char path[64] = {};
    sscanf(req, "%7s %63s", method, path);

    // CORS preflight
    if (strcmp(method, "OPTIONS") == 0) {
        send_and_close(pcb, cs, HTTP_OPTIONS, strlen(HTTP_OPTIONS));
        return;
    }

    // Find body (after \r\n\r\n)
    const char* body = strstr(req, "\r\n\r\n");
    if (body) body += 4; else body = "";

    // --- GET / ---
    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        // Large response — use streaming
        start_streaming_response(pcb, cs, WEB_PAGE, strlen(WEB_PAGE));
        return;  // cs stays alive for callbacks, don't delete
    }

    // --- GET /api/status ---
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        char json[512];
        int n = snprintf(json, sizeof(json),
            "{"
            "\"weights\":[%.1f,%.1f,%.1f],"
            "\"selected_scale\":%d,"
            "\"target_grams\":%d,"
            "\"dispensing\":%s,"
            "\"dispense_done\":%s,"
            "\"dispensed_grams\":%.1f,"
            "\"scale_calibrated\":[%s,%s,%s]"
            "}",
            g_state->weights[0], g_state->weights[1], g_state->weights[2],
            g_state->selected_scale,
            g_state->target_grams,
            g_state->dispensing ? "true" : "false",
            g_state->dispense_done ? "true" : "false",
            g_state->dispensed_grams,
            g_state->scale_calibrated[0] ? "true" : "false",
            g_state->scale_calibrated[1] ? "true" : "false",
            g_state->scale_calibrated[2] ? "true" : "false"
        );
        send_json_response(pcb, cs, json, n);
        return;
    }

    // --- POST routes ---
    if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/dispense") == 0) {
            char action[16];
            if (parse_string_field(body, "action", action, sizeof(action))) {
                if (strcmp(action, "start") == 0) {
                    g_state->pending_command = WebCommand::StartDispense;
                } else if (strcmp(action, "stop") == 0) {
                    g_state->pending_command = WebCommand::StopDispense;
                }
            }
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/target") == 0) {
            g_state->cmd_target = parse_int_field(body, "target");
            g_state->pending_command = WebCommand::SetTarget;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/tare") == 0) {
            g_state->pending_command = WebCommand::Tare;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/select-scale") == 0) {
            g_state->cmd_scale = parse_int_field(body, "scale");
            g_state->pending_command = WebCommand::SelectScale;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/servo") == 0) {
            g_state->cmd_servo_angle = parse_float_field(body, "angle");
            g_state->pending_command = WebCommand::TestServo;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/vibrator") == 0) {
            g_state->cmd_vib_intensity = parse_float_field(body, "intensity");
            g_state->pending_command = WebCommand::TestVibrator;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/stop") == 0) {
            g_state->pending_command = WebCommand::TestStop;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/calibrate") == 0) {
            g_state->cmd_cal_weight = parse_int_field(body, "weight");
            g_state->pending_command = WebCommand::Calibrate;
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }
    }

    // 404 for anything else
    send_and_close(pcb, cs, HTTP_404, strlen(HTTP_404));
}

// ---------- lwIP TCP callbacks -----------------------------------------------

static err_t tcp_recv_cb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    ConnState* cs = (ConnState*)arg;

    if (!p) {
        // Connection closed by client
        if (cs) {
            delete cs;
            tcp_arg(pcb, nullptr);
        }
        tcp_close(pcb);
        return ERR_OK;
    }

    // Accumulate data
    int copy_len = p->tot_len;
    if (cs->req_len + copy_len >= REQ_BUF_SIZE - 1) {
        copy_len = REQ_BUF_SIZE - 1 - cs->req_len;
    }
    if (copy_len > 0) {
        pbuf_copy_partial(p, cs->req_buf + cs->req_len, copy_len, 0);
        cs->req_len += copy_len;
        cs->req_buf[cs->req_len] = '\0';
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Check if we have a complete request (headers end with \r\n\r\n)
    if (strstr(cs->req_buf, "\r\n\r\n")) {
        // Don't delete cs here — handle_request may keep it for streaming
        handle_request(pcb, cs);
    }

    return ERR_OK;
}

static void tcp_err_cb(void* arg, err_t err) {
    ConnState* cs = (ConnState*)arg;
    if (cs) delete cs;
}

static err_t tcp_accept_cb(void* arg, struct tcp_pcb* newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    tcp_setprio(newpcb, TCP_PRIO_MIN);

    ConnState* cs = new ConnState();
    cs->req_len = 0;
    cs->req_buf[0] = '\0';
    cs->send_data = nullptr;
    cs->send_total = 0;
    cs->send_offset = 0;
    cs->resp_body = nullptr;
    cs->resp_body_len = 0;
    cs->resp_header_len = 0;
    cs->header_done = false;

    tcp_arg(newpcb, cs);
    tcp_recv(newpcb, tcp_recv_cb);
    tcp_err(newpcb, tcp_err_cb);

    return ERR_OK;
}

// ---------- public API -------------------------------------------------------

void web_server_init(DispenserState* state, uint16_t port) {
    g_state = state;

    struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        printf("[web] failed to create PCB\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err != ERR_OK) {
        printf("[web] bind failed: %d\n", err);
        return;
    }

    pcb = tcp_listen_with_backlog(pcb, 4);
    if (!pcb) {
        printf("[web] listen failed\n");
        return;
    }

    tcp_accept(pcb, tcp_accept_cb);
    printf("[web] listening on port %d\n", port);
}
