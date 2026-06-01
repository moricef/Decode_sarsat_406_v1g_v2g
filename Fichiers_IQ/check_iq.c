#include <complex.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  FILE *f = fopen("~/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_V10.2/"
                  "Fichiers_IQ/test_known.iq",
                  "rb");
  if (!f) {
    perror("Failed to open file");
    return 1;
  }

  float i, q;
  float max_i = -FLT_MAX, min_i = FLT_MAX, max_q = -FLT_MAX, min_q = FLT_MAX;
  size_t count = 0;

  while (fread(&i, sizeof(float), 1, f) == 1 &&
         fread(&q, sizeof(float), 1, f) == 1) {
    if (i > max_i)
      max_i = i;
    if (i < min_i)
      min_i = i;
    if (q > max_q)
      max_q = q;
    if (q < min_q)
      min_q = q;
    count++;
  }

  fclose(f);

  printf("Samples read: %zu\n", count);
  printf("I range: [%f, %f]\n", min_i, max_i);
  printf("Q range: [%f, %f]\n", min_q, max_q);

  return 0;
}
