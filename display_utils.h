/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// display_utils.h - Display utilities header
#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <stddef.h>

/**
 * Opens the position in OpenStreetMap browser
 * @param lat Latitude in decimal degrees
 * @param lon Longitude in decimal degrees
 */
void open_osm_map(double lat, double lon);

/**
 * Formats coordinates to UTM string
 * @param lat Latitude in decimal degrees
 * @param lon Longitude in decimal degrees
 * @param buffer Output buffer for UTM string
 * @param size Buffer size
 */
void format_utm_coords(double lat, double lon, char* buffer, size_t size);

/**
 * Formats coordinates to human-readable string
 * @param lat Latitude in decimal degrees
 * @param lon Longitude in decimal degrees
 * @param buffer Output buffer
 * @param size Buffer size
 */
void format_coordinates(double lat, double lon, char* buffer, size_t size);

/**
 * Logs message with timestamp to terminal
 * @param message Message to log
 */
void log_to_terminal(const char* message);

#endif // DISPLAY_UTILS_H
