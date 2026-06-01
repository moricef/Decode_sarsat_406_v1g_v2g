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

/* 1 if the frequency falls in a T.012 Table H.2 distress channel
 * (±2 kHz tolerance), 0 otherwise. */
int scan_alert_freq_authorised(double freq_mhz);

/* Send an alert email. body_text is the full human-readable decode
 * (multi-line). type is "FGB" or "SGB". A summary header (timestamp,
 * type, freq, snr, hex frame) is prepended automatically.
 * Returns 0 on success, -1 if alerts are disabled or sendemail fails. */
int scan_alert_send(const char *type, double freq_mhz, double snr_db,
                    const uint8_t *bits, size_t n_bits,
                    const char *body_text);

#endif /* SCAN_ALERT_H */
