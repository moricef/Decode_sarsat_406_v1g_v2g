/**
 * @file scan_alert.c
 * @brief Email alert + T.012 channel whitelist, ported from scan406.pl.
 */

#include "scan_alert.h"
#include "diag_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* SMTP / recipient settings, populated by scan_alert_load_config().
 * If load failed, alerts_enabled stays 0. */
static char smtp_server[128] = "";
static char smtp_user[128] = "";
static char smtp_pass[128] = "";
static char recipients[256] = "";
static int  alerts_enabled = 0;

/* T.012 Table H.2 distress channels (MHz). Same set as scan406.pl. */
static const double authorised_channels[] = {
    406.025,  /* B */
    406.028,  /* C */
    406.031,  /* D */
    406.037,  /* F */
    406.040,  /* G */
    406.049,  /* J */
    406.052,  /* K */
    406.061,  /* N */
    406.064,  /* O */
    406.073,  /* R */
    406.076,  /* S */
};
#define N_AUTH_CHAN (sizeof(authorised_channels) / sizeof(authorised_channels[0]))
#define CHAN_TOL_MHZ 0.002  /* ±2 kHz */

static int alert_mode_downlink(void);
static int parse_alert_center(double *lat, double *lon);
static double alert_radius_km(void);

static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) {
        s[--n] = '\0';
    }
}

int scan_alert_load_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        DWARN("scan_alert: config %s not found, alerts disabled\n", path);
        return -1;
    }
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        rstrip(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if      (!strcmp(key, "smtp_serveur"))   snprintf(smtp_server, sizeof smtp_server, "%s", val);
        else if (!strcmp(key, "utilisateur"))    snprintf(smtp_user,   sizeof smtp_user,   "%s", val);
        else if (!strcmp(key, "password"))       snprintf(smtp_pass,   sizeof smtp_pass,   "%s", val);
        else if (!strcmp(key, "destinataires"))  snprintf(recipients,  sizeof recipients,  "%s", val);
    }
    fclose(fp);

    if (!smtp_server[0] || !smtp_user[0] || !smtp_pass[0] || !recipients[0]) {
        DWARN("scan_alert: config %s incomplete, alerts disabled\n", path);
        return -1;
    }
    alerts_enabled = 1;
    return 0;
}

void scan_alert_print_config_summary(void) {
    printf("  mail smtp : %s\n", smtp_server);
    printf("  mail user : %s\n", smtp_user);
    printf("  mail to   : %s\n", recipients);
    printf("  alert mode: %s\n", alert_mode_downlink() ? "downlink" : "direct");
    if (alert_mode_downlink()) {
        double lat = 0.0, lon = 0.0;
        double radius = alert_radius_km();
        if (parse_alert_center(&lat, &lon) && radius > 0.0) {
            printf("  alert area: %.5f,%.5f radius %.0f km\n", lat, lon, radius);
        } else {
            printf("  alert area: invalid\n");
        }
    }
}

const char *scan_alert_extract_hex_id(const char *body) {
    if (!body) return NULL;
    const char *p = strstr(body, "Hex ID:");
    if (!p) return NULL;
    p += 7;  /* strlen("Hex ID:") */
    while (*p == ' ' || *p == '\t') p++;
    static char id[64];
    size_t n = 0;
    while (n < sizeof(id) - 1 && *p && *p != '\n' && *p != '\r' && *p != ' ') {
        id[n++] = *p++;
    }
    id[n] = '\0';
    return n > 0 ? id : NULL;
}

/* Repetition tracker. Real distress beacons repeat every ~50 s for
 * hours; bench tests and one-shot transmissions don't. Confirming a
 * second sighting of the same Hex ID within REPEAT_WINDOW_SEC
 * filters out single-burst false positives (e.g. Airbus ground tests
 * on channel B observed at firmin). */
#define REPEAT_MAX_SEEN     64
#define REPEAT_WINDOW_SEC   180   /* 3 minutes */

static struct {
    char   hex_id[64];
    time_t last_seen;
} seen[REPEAT_MAX_SEEN];
static int seen_n = 0;

int scan_alert_is_repeat(const char *hex_id) {
    if (!hex_id || !*hex_id) return 0;
    time_t now = time(NULL);

    for (int i = 0; i < seen_n; i++) {
        if (strcmp(seen[i].hex_id, hex_id) == 0) {
            int delta = (int)(now - seen[i].last_seen);
            seen[i].last_seen = now;
            return (delta <= REPEAT_WINDOW_SEC) ? 1 : 0;
        }
    }

    /* New entry. Evict the oldest if the table is full. */
    int slot;
    if (seen_n < REPEAT_MAX_SEEN) {
        slot = seen_n++;
    } else {
        slot = 0;
        time_t oldest = seen[0].last_seen;
        for (int i = 1; i < REPEAT_MAX_SEEN; i++) {
            if (seen[i].last_seen < oldest) {
                oldest = seen[i].last_seen;
                slot = i;
            }
        }
    }
    snprintf(seen[slot].hex_id, sizeof seen[slot].hex_id, "%s", hex_id);
    seen[slot].last_seen = now;
    return 0;
}

int scan_alert_freq_authorised(double freq_mhz) {
    for (size_t i = 0; i < N_AUTH_CHAN; i++) {
        double c = authorised_channels[i];
        if (freq_mhz >= c - CHAN_TOL_MHZ && freq_mhz <= c + CHAN_TOL_MHZ)
            return 1;
    }
    return 0;
}

static int alert_mode_downlink(void) {
    const char *mode = getenv("DEC406_ALERT_MODE");
    return mode && strcmp(mode, "downlink") == 0;
}

static int parse_alert_center(double *lat, double *lon) {
    const char *e = getenv("DEC406_ALERT_CENTER");
    if (!e || !*e) return 0;
    char *end = NULL;
    double a = strtod(e, &end);
    if (end == e || *end != ',') return 0;
    const char *lon_start = end + 1;
    double b = strtod(lon_start, &end);
    if (end == lon_start || (end && *end != '\0')) return 0;
    if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) return 0;
    *lat = a;
    *lon = b;
    return 1;
}

static double alert_radius_km(void) {
    const char *e = getenv("DEC406_ALERT_RADIUS_KM");
    if (!e || !*e) return -1.0;
    char *end = NULL;
    double r = strtod(e, &end);
    if (end == e || (end && *end != '\0') || r <= 0.0) return -1.0;
    return r;
}

static int parse_position_line(const char *line, double *lat, double *lon) {
    const char *p = strchr(line, ':');
    if (!p) return 0;
    p++;

    char *end = NULL;
    double a = strtod(p, &end);
    if (end == p) return 0;
    p = end;
    while (*p && *p != 'N' && *p != 'S') p++;
    if (*p != 'N' && *p != 'S') return 0;
    char ns = *p++;

    while (*p && *p != ',') p++;
    if (*p != ',') return 0;
    p++;

    double b = strtod(p, &end);
    if (end == p) return 0;
    p = end;
    while (*p && *p != 'E' && *p != 'W') p++;
    if (*p != 'E' && *p != 'W') return 0;
    char ew = *p;

    if (ns == 'S') a = -a;
    if (ew == 'W') b = -b;
    if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) return 0;
    *lat = a;
    *lon = b;
    return 1;
}

static int scan_alert_extract_position(const char *body, double *lat, double *lon) {
    if (!body) return 0;
    const char *keys[] = {
        "Composite position:",
        "Position:",
        "Coordinates:",
        "Position (PDF-1):",
        NULL
    };

    for (int k = 0; keys[k]; k++) {
        const char *p = body;
        while ((p = strstr(p, keys[k])) != NULL) {
            const char *end = strchr(p, '\n');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            char line[160];
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            if (parse_position_line(line, lat, lon))
                return 1;
            p += strlen(keys[k]);
        }
    }
    return 0;
}

static double haversine_km(double lat1, double lon1, double lat2, double lon2) {
    const double r = 6371.0;
    double p1 = lat1 * M_PI / 180.0;
    double p2 = lat2 * M_PI / 180.0;
    double dp = (lat2 - lat1) * M_PI / 180.0;
    double dl = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dp / 2.0) * sin(dp / 2.0) +
               cos(p1) * cos(p2) * sin(dl / 2.0) * sin(dl / 2.0);
    return 2.0 * r * atan2(sqrt(a), sqrt(1.0 - a));
}

int scan_alert_channel_allows(double freq_mhz, const char *body_text) {
    if (!alert_mode_downlink())
        return scan_alert_freq_authorised(freq_mhz);

    double center_lat = 0.0, center_lon = 0.0;
    double radius = alert_radius_km();
    if (!parse_alert_center(&center_lat, &center_lon) || radius <= 0.0) {
        DWARN("scan_alert: downlink mode needs DEC406_ALERT_CENTER=lat,lon "
              "and DEC406_ALERT_RADIUS_KM\n");
        return 0;
    }

    double lat = 0.0, lon = 0.0;
    if (!scan_alert_extract_position(body_text, &lat, &lon)) {
        DIAG("scan_alert: downlink alert skipped (no valid decoded position)\n");
        return 0;
    }

    double d = haversine_km(center_lat, center_lon, lat, lon);
    if (d > radius) {
        DIAG("scan_alert: downlink alert skipped "
             "(position %.5f,%.5f is %.0f km from center, radius %.0f km)\n",
             lat, lon, d, radius);
        return 0;
    }

    DIAG("scan_alert: downlink alert allowed "
         "(position %.5f,%.5f is %.0f km from center, radius %.0f km)\n",
         lat, lon, d, radius);
    return 1;
}

/* MSB-first hex of n_bits bits. Buffer must hold (n_bits+3)/4 + 1 bytes.
 * Trailing bits in the last nibble are padded with zeros. */
static void bits_to_hex(const uint8_t *bits, size_t n_bits, char *out) {
    size_t n_nib = (n_bits + 3) / 4;
    for (size_t i = 0; i < n_nib; i++) {
        unsigned v = 0;
        for (int j = 0; j < 4; j++) {
            size_t bi = i * 4 + j;
            unsigned b = (bi < n_bits) ? (bits[bi] & 1u) : 0u;
            v = (v << 1) | b;
        }
        out[i] = (char)(v < 10 ? '0' + v : 'A' + v - 10);
    }
    out[n_nib] = '\0';
}

int scan_alert_send(const char *type, double freq_mhz, double snr_db,
                    const uint8_t *bits, size_t n_bits,
                    const char *body_text) {
    if (!alerts_enabled) return -1;

    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char utc[64];
    strftime(utc, sizeof utc, "%Y-%m-%d %H:%M:%S UTC", &tm_utc);

    /* Unique body file name: sendemail runs in the background so we may
     * fire multiple alerts before the previous one finishes — would
     * otherwise overwrite the in-flight body. */
    static unsigned counter = 0;
    char body_path[96];
    snprintf(body_path, sizeof body_path,
             "/tmp/scan406_mail_%d_%lld_%u.txt",
             (int)getpid(), (long long)now, ++counter);
    FILE *fp = fopen(body_path, "w");
    if (!fp) {
        DWARN("scan_alert: cannot open %s\n", body_path);
        return -1;
    }

    char hex[256];
    bits_to_hex(bits, n_bits, hex);

    fprintf(fp,
            "Date     : %s\n"
            "Type     : %s\n"
            "Frequence: %.4f MHz\n"
            "SNR      : %.0f dB\n"
            "Hex frame: %s\n"
            "\n"
            "%s",
            utc, type, freq_mhz, snr_db, hex, body_text);
    fclose(fp);

    /* sendemail in the background so the TLS handshake (~1-2 s) doesn't
     * block the decode pipeline (caused ring overruns on SGB bursts).
     * The shell takes care of removing the body file after sending. */
    char subject[128];
    snprintf(subject, sizeof subject,
             "Alerte_Balise_406 [%s] f=%.4f MHz", type, freq_mhz);

    char cmd[2048];
    snprintf(cmd, sizeof cmd,
             "( sendemail -f '%s' -u '%s' -t '%s' -s '%s' "
             "-o tls=yes -o message-file='%s' "
             "-xu '%s' -xp '%s' 2>/dev/null 1>/dev/null; "
             "rm -f '%s' ) &",
             smtp_user, subject, recipients, smtp_server,
             body_path, smtp_user, smtp_pass, body_path);

    int rc = system(cmd);
    if (rc != 0) {
        DWARN("scan_alert: shell fork failed (rc=%d) for %.4f MHz\n", rc, freq_mhz);
        return -1;
    }
    return 0;
}
