// display_utils.h
#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Ouvre la position dans OpenStreetMap
void open_osm_map(double lat, double lon);

// Convertit et formate les coordonnées UTM
void format_utm_coords(double lat, double lon, char* buffer, size_t size);

// Journalisation dans le terminal
void log_to_terminal(const char* message);

// Formate les coordonnées décimales
void format_coordinates(double lat, double lon, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_UTILS_H
