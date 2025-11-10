/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// display_utils.c
#include "display_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =================================================================
// 1. open_osm_map() - Affiche un lien cliquable dans le terminal
// =================================================================
void open_osm_map(double lat, double lon) {
    printf("📍 OpenStreetMap: \033]8;;https://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=18/%.5f/%.5f\033\\https://www.openstreetmap.org/?mlat=%.5f&mlon=%.5f#map=18/%.5f/%.5f\033]8;;\033\\\n",
           lat, lon, lat, lon, lat, lon, lat, lon);
}

// =================================================================
// 2. format_utm_coords() - Remplacement de GeogToUTM()
// =================================================================
void format_utm_coords(double lat, double lon, char* buffer, size_t size) {
    // Constantes UTM
    const double k0 = 0.9996;
    const double a = 6378137.0;
    const double eccSquared = 0.00669438;
    
    double long_temp = lon;
    if (lon < 0) long_temp += 360;  // Conversion longitude positive
    
    int zone = (int)((long_temp + 180) / 6) + 1;
    
    // Conversion en radians
    double lat_rad = lat * M_PI / 180.0;
    double lon_rad = lon * M_PI / 180.0;
    double lon0_rad = ((zone * 6 - 183) * M_PI) / 180.0;
    
    // Calculs UTM
    double N = a / sqrt(1 - eccSquared * sin(lat_rad) * sin(lat_rad));
    double T = tan(lat_rad) * tan(lat_rad);
    double C = eccSquared * cos(lat_rad) * cos(lat_rad);
    double A = cos(lat_rad) * (lon_rad - lon0_rad);
    
    double M = a * ((1 - eccSquared/4 - 3*eccSquared*eccSquared/64 - 5*eccSquared*eccSquared*eccSquared/256) * lat_rad
                   - (3*eccSquared/8 + 3*eccSquared*eccSquared/32 + 45*eccSquared*eccSquared*eccSquared/1024) * sin(2*lat_rad)
                   + (15*eccSquared*eccSquared/256 + 45*eccSquared*eccSquared*eccSquared/1024) * sin(4*lat_rad)
                   - (35*eccSquared*eccSquared*eccSquared/3072) * sin(6*lat_rad));
    
    double easting = k0 * N * (A + (1 - T + C) * A*A*A/6
                   + (5 - 18*T + T*T + 72*C - 58*eccSquared) * A*A*A*A*A/120) + 500000.0;
    
    double northing = k0 * (M + N * tan(lat_rad) * (A*A/2 + (5 - T + 9*C + 4*C*C) * A*A*A*A/24
                   + (61 - 58*T + T*T + 600*C - 330*eccSquared) * A*A*A*A*A*A/720));
    
    if (lat < 0) northing += 10000000.0;  // Correction hémisphère sud
    
    // Formatage dans le buffer
    snprintf(buffer, size, "UTM Zone %d%c | Easting: %.2fm | Northing: %.2fm",
             zone, (lat >= 0) ? 'N' : 'S', easting, northing);
}

// =================================================================
// 3. Fonctions utilitaires supplémentaires
// =================================================================
void log_to_terminal(const char* message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d:%02d] %s\n", t->tm_hour, t->tm_min, t->tm_sec, message);
}

void format_coordinates(double lat, double lon, char* buffer, size_t size) {
    char ns = (lat >= 0) ? 'N' : 'S';
    char ew = (lon >= 0) ? 'E' : 'W';
    snprintf(buffer, size, "%.5f°%c, %.5f°%c", fabs(lat), ns, fabs(lon), ew);
}
