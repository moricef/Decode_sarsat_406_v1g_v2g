/**
 * @file scan_alert.h
 * @brief Email alert on decoded 406 MHz beacon, T.012 channel whitelist.
 *
 * Ports the scan406.pl alerting behaviour: when a beacon decodes on an
 * authorised distress channel (Table H.2 of T.012), send an email via
 * `sendemail` with the human-readable decode block as the body.
 *
 * The decoder remains running and prints to stdout regardless; the alert
 * is a side-effect emitted only when the frequency is whitelisted and a
 * configuration file is present.
 */
#ifndef SCAN_ALERT_H
#define SCAN_ALERT_H

#include <stddef.h>
#include <stdint.h>

/* Load SMTP / recipient settings from a key=value file.
 *   smtp_serveur=smtp.gmail.com:587
 *   utilisateur=user@example.org
 *   password=...
 *   destinataires=a@b.com,c@d.fr
 * Returns 0 on success, -1 if the file is absent or unreadable
 * (alerts are then disabled). Safe to call once at startup. */
int scan_alert_load_config(const char *path);

/* Print the loaded SMTP/user/recipient settings without the password.
 * Intended for scanner startup logs, after scan_alert_load_config()
 * returned success. */
void scan_alert_print_config_summary(void);

/* 1 if the frequency falls in a T.012 Table H.2 distress channel
 * (±2 kHz tolerance), 0 otherwise. */
int scan_alert_freq_authorised(double freq_mhz);

/* Alert routing policy.
 * Default mode: direct 406 MHz, require a whitelisted T.012 channel.
 * Downlink mode: set DEC406_ALERT_MODE=downlink and require a decoded
 * position inside DEC406_ALERT_CENTER=lat,lon and DEC406_ALERT_RADIUS_KM.
 * Returns 1 if an otherwise valid alert may be mailed. */
int scan_alert_channel_allows(double freq_mhz, const char *body_text);

/* Send an alert email. body_text is the full human-readable decode
 * (multi-line). type is "FGB" or "SGB". A summary header (timestamp,
 * type, freq, snr, hex frame) is prepended automatically.
 * Returns 0 on success, -1 if alerts are disabled or sendemail fails. */
int scan_alert_send(const char *type, double freq_mhz, double snr_db,
                    const uint8_t *bits, size_t n_bits,
                    const char *body_text);

/* Extract the Hex ID from a decode block (matches the "Hex ID:" line
 * printed by dec406_v1g/dec406_v2g). Returns a pointer into a static
 * buffer (overwritten on next call), or NULL if no Hex ID line is
 * found. */
const char *scan_alert_extract_hex_id(const char *body);

/* Repetition tracker: returns 1 if the given Hex ID was already seen
 * within the last REPEAT_WINDOW_SEC seconds (confirmed repetition,
 * caller should send the alert), 0 otherwise (first sighting or stale
 * entry — record and stay silent). Discriminates real beacons (which
 * repeat every ~50 s) from one-shot bench tests. */
int scan_alert_is_repeat(const char *hex_id);

#endif /* SCAN_ALERT_H */
