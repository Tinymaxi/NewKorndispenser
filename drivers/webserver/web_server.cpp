#include "web_server.h"
#include "web_page.h"
#include "dispenser_state.h"
#include "telemetry.hpp"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

static DispenserState* g_state = nullptr;

// Push a command onto the ring queue (runs in lwIP callback context; the main
// loop drains under the lwIP lock, so head/tail can't race). Drops when full.
static void push_cmd(WebCommand cmd, int i0 = 0, float f0 = 0, float f1 = 0, float f2 = 0,
                     const char* s0 = nullptr) {
    uint8_t next = (uint8_t)((g_state->cmd_head + 1) % WEBCMD_QUEUE_LEN);
    if (next == g_state->cmd_tail) return;   // full - drop newest
    WebCmd& slot = g_state->cmd_queue[g_state->cmd_head];
    slot.cmd = cmd;
    slot.i0 = i0;
    slot.f0 = f0;
    slot.f1 = f1;
    slot.f2 = f2;
    slot.s0[0] = '\0';
    if (s0) {
        strncpy(slot.s0, s0, sizeof(slot.s0) - 1);
        slot.s0[sizeof(slot.s0) - 1] = '\0';
    }
    g_state->cmd_head = next;                // publish after the slot is written
}

// Scale names travel through JSON, the CSV metadata line (comma/equals
// delimited) and the HD44780 LCD: keep printable ASCII, drop " \ , =
static void sanitize_name(char* s) {
    char* w = s;
    for (char* r = s; *r; ++r) {
        char c = *r;
        if (c < 0x20 || c > 0x7E) continue;
        if (c == '"' || c == '\\' || c == ',' || c == '=') continue;
        *w++ = c;
    }
    *w = '\0';
}

// ---------- helpers ----------------------------------------------------------

static bool has_field(const char* body, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    return strstr(body, search) != nullptr;
}

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

    // CSV log streaming (/api/log.csv) - rows are generated on the fly from the
    // telemetry buffer, one line at a time, into csv_buf.
    enum class SendMode : uint8_t { FlashBody, CsvLog };
    SendMode      mode      = SendMode::FlashBody;
    TelemetryMeta csv_meta  = {};   // snapshot taken at request time
    uint32_t      csv_row   = 0;    // next sample index to format
    uint8_t       csv_phase = 0;    // 0 = metadata+header lines, 1 = rows, 2 = done
    char          csv_buf[256];     // one formatted line (or the header block)
    int           csv_len   = 0;
    int           csv_off   = 0;
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

    // Then send body (no COPY flag — body points to static const data in flash)
    while (cs->send_offset < cs->resp_body_len) {
        int avail = (int)tcp_sndbuf(pcb);
        if (avail == 0) {
            tcp_output(pcb);
            return ERR_OK;  // Wait for tcp_sent callback
        }
        int chunk = cs->resp_body_len - cs->send_offset;
        if (chunk > avail) chunk = avail;
        err_t err = tcp_write(pcb, cs->resp_body + cs->send_offset, chunk, 0);
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

// CSV analogue of send_more(): generates the response line by line from the
// telemetry buffer. Unlike the flash-body path, lines live in a reused per-
// connection buffer, so every tcp_write MUST copy; and ERR_MEM means "flush and
// retry on the next tcp_sent callback", not "abort".
static err_t send_more_csv(struct tcp_pcb* pcb, ConnState* cs) {
    // HTTP header first
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
            if (err == ERR_MEM) {
                tcp_output(pcb);
                return ERR_OK;  // Retry on next tcp_sent callback
            }
            if (err != ERR_OK) {
                cleanup_conn(pcb, cs);
                return ERR_OK;
            }
            cs->send_offset += chunk;
        }
        cs->header_done = true;
        cs->csv_len = 0;
        cs->csv_off = 0;
    }

    while (true) {
        // Flush the pending line
        while (cs->csv_off < cs->csv_len) {
            int avail = (int)tcp_sndbuf(pcb);
            if (avail == 0) {
                tcp_output(pcb);
                return ERR_OK;
            }
            int chunk = cs->csv_len - cs->csv_off;
            if (chunk > avail) chunk = avail;
            err_t err = tcp_write(pcb, cs->csv_buf + cs->csv_off, chunk, TCP_WRITE_FLAG_COPY);
            if (err == ERR_MEM) {
                tcp_output(pcb);
                return ERR_OK;
            }
            if (err != ERR_OK) {
                cleanup_conn(pcb, cs);
                return ERR_OK;
            }
            cs->csv_off += chunk;
        }

        // Line drained - generate the next one
        if (cs->csv_phase == 0) {
            const TelemetryMeta& m = cs->csv_meta;
            cs->csv_len = snprintf(cs->csv_buf, sizeof(cs->csv_buf),
                "# korndispenser-pid-log v2\n"
                "# run_id=%u,scale=%u,name=%s,target_g=%u,kp=%.3f,ki=%.4f,kd=%.3f,"
                "samples=%u,final_g=%.1f,sample_ms=100\n"
                "t_ms,setpoint_g,dispensed_g,weight_g,gross_g,servo_deg,p_term,i_term,d_term,vib\n",
                (unsigned)m.run_id, (unsigned)(m.scale + 1), m.name, (unsigned)m.target_g,
                (double)m.kp, (double)m.ki, (double)m.kd,
                (unsigned)m.count, (double)m.final_g);
            cs->csv_off = 0;
            cs->csv_phase = 1;
        } else if (cs->csv_phase == 1) {
            // A new dispense resets the buffer - abandon the stream (short read)
            if (telem_meta().run_id != cs->csv_meta.run_id) {
                tcp_output(pcb);
                cleanup_conn(pcb, cs);
                return ERR_OK;
            }
            if (cs->csv_row >= cs->csv_meta.count) {
                cs->csv_phase = 2;
                continue;
            }
            const TelemetrySample* s = telem_sample(cs->csv_row);
            if (!s) {
                cs->csv_phase = 2;
                continue;
            }
            cs->csv_len = snprintf(cs->csv_buf, sizeof(cs->csv_buf),
                "%lu,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%.2f\n",
                (unsigned long)s->t_ms,
                (double)s->setpoint, (double)s->dispensed, (double)s->weight,
                (double)s->gross,
                (double)s->servo, (double)s->p, (double)s->i, (double)s->d,
                (double)s->vib);
            cs->csv_off = 0;
            cs->csv_row++;
        } else {
            // All rows sent - flush and close
            tcp_output(pcb);
            cleanup_conn(pcb, cs);
            return ERR_OK;
        }
    }
}

static err_t tcp_sent_cb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    ConnState* cs = (ConnState*)arg;
    if (!cs) return ERR_OK;
    if (cs->mode == ConnState::SendMode::CsvLog) return send_more_csv(pcb, cs);
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

// Start the streamed CSV telemetry response
static void start_csv_response(struct tcp_pcb* pcb, ConnState* cs) {
    TelemetryMeta m = telem_meta();
    if (m.run_id == 0) {
        // No dispense has ever run - nothing to export
        send_and_close(pcb, cs, HTTP_404, strlen(HTTP_404));
        return;
    }

    cs->mode = ConnState::SendMode::CsvLog;
    cs->csv_meta = m;   // snapshot: rows < m.count are immutable for this run_id
    cs->csv_row = 0;
    cs->csv_phase = 0;
    cs->csv_len = 0;
    cs->csv_off = 0;

    // Length is delimited by connection close (no Content-Length).
    cs->resp_header_len = snprintf(cs->resp_header, sizeof(cs->resp_header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv\r\n"
        "Content-Disposition: attachment; filename=\"pid_run_%u.csv\"\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        (unsigned)m.run_id);
    cs->header_done = false;
    cs->send_offset = 0;

    tcp_sent(pcb, tcp_sent_cb);
    send_more_csv(pcb, cs);
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
        // Get WiFi signal strength (meaningless in AP mode - UI hides it)
        int32_t rssi = -100;
        cyw43_wifi_get_rssi(&cyw43_state, &rssi);

        TelemetryMeta tm = telem_meta();

        char json[1024];
        int n = snprintf(json, sizeof(json),
            "{"
            "\"weights\":[%.1f,%.1f,%.1f],"
            "\"gross\":[%.1f,%.1f,%.1f],"
            "\"names\":[\"%s\",\"%s\",\"%s\"],"
            "\"selected_scale\":%d,"
            "\"target_grams\":%d,"
            "\"dispensing\":%s,"
            "\"dispense_done\":%s,"
            "\"dispensed_grams\":%.1f,"
            "\"scale_calibrated\":[%s,%s,%s],"
            "\"szero\":[%.0f,%.0f,%.0f],"
            "\"pid\":{\"kp\":%.3f,\"ki\":%.4f,\"kd\":%.3f},"
            "\"servo\":%.1f,\"vib\":%.2f,"
            "\"rssi\":%ld,"
            "\"run\":{\"id\":%u,\"samples\":%u,\"active\":%s},"
            "\"mode\":\"%s\""
            "}",
            g_state->weights[0], g_state->weights[1], g_state->weights[2],
            g_state->gross[0], g_state->gross[1], g_state->gross[2],
            g_state->names[0], g_state->names[1], g_state->names[2],
            g_state->selected_scale,
            g_state->target_grams,
            g_state->dispensing ? "true" : "false",
            g_state->dispense_done ? "true" : "false",
            g_state->dispensed_grams,
            g_state->scale_calibrated[0] ? "true" : "false",
            g_state->scale_calibrated[1] ? "true" : "false",
            g_state->scale_calibrated[2] ? "true" : "false",
            (double)g_state->servo_zero[0], (double)g_state->servo_zero[1],
            (double)g_state->servo_zero[2],
            (double)g_state->pid_kp, (double)g_state->pid_ki, (double)g_state->pid_kd,
            (double)g_state->servo_angle, (double)g_state->vib_intensity,
            (long)rssi,
            (unsigned)tm.run_id, (unsigned)tm.count, tm.active ? "true" : "false",
            g_state->ap_mode ? "ap" : "sta"
        );
        send_json_response(pcb, cs, json, n);
        return;
    }

    // --- GET /api/log.csv ---
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/log.csv") == 0) {
        start_csv_response(pcb, cs);
        return;  // cs stays alive for callbacks on success
    }

    // --- POST routes ---
    if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/dispense") == 0) {
            char action[16];
            if (parse_string_field(body, "action", action, sizeof(action))) {
                if (strcmp(action, "start") == 0) {
                    push_cmd(WebCommand::StartDispense);
                } else if (strcmp(action, "stop") == 0) {
                    push_cmd(WebCommand::StopDispense);
                }
            }
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/target") == 0) {
            push_cmd(WebCommand::SetTarget, parse_int_field(body, "target"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/tare") == 0) {
            push_cmd(WebCommand::Tare);
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/select-scale") == 0) {
            push_cmd(WebCommand::SelectScale, parse_int_field(body, "scale"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/servo") == 0) {
            // Optional "servo" field addresses a specific servo (calibration
            // UI); without it the command follows the selected scale (-1)
            int servo = has_field(body, "servo") ? parse_int_field(body, "servo") : -1;
            push_cmd(WebCommand::TestServo, servo, parse_float_field(body, "angle"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/vibrator") == 0) {
            push_cmd(WebCommand::TestVibrator, 0, parse_float_field(body, "intensity"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/test/stop") == 0) {
            push_cmd(WebCommand::TestStop);
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/pid") == 0) {
            push_cmd(WebCommand::SetPID, 0,
                     parse_float_field(body, "kp"),
                     parse_float_field(body, "ki"),
                     parse_float_field(body, "kd"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/calibrate") == 0) {
            push_cmd(WebCommand::Calibrate, parse_int_field(body, "weight"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/servo/zero") == 0) {
            push_cmd(WebCommand::SetServoZero,
                     parse_int_field(body, "servo"),
                     parse_float_field(body, "angle"));
            send_and_close(pcb, cs, HTTP_204, strlen(HTTP_204));
            return;
        }

        if (strcmp(path, "/api/name") == 0) {
            char name[16] = {0};
            parse_string_field(body, "name", name, sizeof(name));
            sanitize_name(name);
            int scale = parse_int_field(body, "scale");
            if (scale >= 0 && scale <= 2) {
                push_cmd(WebCommand::SetName, scale, 0, 0, 0, name);
            }
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

    // Check if we have a complete request (headers + full body)
    const char* hdr_end = strstr(cs->req_buf, "\r\n\r\n");
    if (hdr_end) {
        int body_offset = (hdr_end - cs->req_buf) + 4;
        int body_received = cs->req_len - body_offset;

        // Check Content-Length to see if we need to wait for body data
        int content_length = 0;
        const char* cl = strstr(cs->req_buf, "Content-Length:");
        if (!cl) cl = strstr(cs->req_buf, "content-length:");
        if (cl) {
            cl += 15; // skip "Content-Length:"
            while (*cl == ' ') cl++;
            content_length = atoi(cl);
        }

        if (body_received >= content_length) {
            // Don't delete cs here — handle_request may keep it for streaming
            handle_request(pcb, cs);
        }
        // else: wait for more data in next tcp_recv_cb call
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
