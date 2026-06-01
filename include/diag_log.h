/**
 * @file diag_log.h
 * @brief Stderr logging with systemd journal priority prefixes.
 *
 * systemd reads the kernel-syslog level prefix "<N>" at the start of a
 * stderr line and stores it as PRIORITY=N in the journal. This lets the
 * decode pipeline emit verbose diagnostics on stderr that journalctl /
 * lazyjournal can filter out (default view = priority <= info), while
 * keeping real errors visible.
 *
 *   DIAG(fmt, ...)  → PRIORITY=debug (7)     verbose pipeline trace
 *   DWARN(fmt, ...) → PRIORITY=warning (4)   recoverable anomalies
 *   DERR(fmt, ...)  → PRIORITY=err (3)       fatal errors
 *
 * The format string MUST already include the trailing '\n'. The "<N>"
 * prefix is consumed by systemd when the line is fed to its journal
 * stream; when running outside systemd, the prefix appears literally on
 * stderr — harmless, easy to grep out.
 *
 * For multi-call sequences that compose a single logical line (e.g. a
 * loop of fprintf(stderr, "%x", ...) followed by a final "\n"), only the
 * FIRST call carries the "<N>" prefix.
 */
#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stdio.h>

#define DIAG(fmt, ...)  fprintf(stderr, "<7>" fmt, ##__VA_ARGS__)
#define DWARN(fmt, ...) fprintf(stderr, "<4>" fmt, ##__VA_ARGS__)
#define DERR(fmt, ...)  fprintf(stderr, "<3>" fmt, ##__VA_ARGS__)

#endif /* DIAG_LOG_H */
