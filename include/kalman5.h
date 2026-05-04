/**
 * @file kalman5.h
 * @brief 5-state Kalman filter for joint carrier/code tracking.
 *
 * State vector: [carrier_phase, carrier_freq, freq_rate, code_phase, code_rate]
 * Measurement:  [delta_phase, delta_freq, delta_tau]
 *
 * Per Zhang et al. (ICEE 2025), the state transition couples carrier frequency
 * into code rate via f_chip/f_carrier (Doppler-induced code drift).
 */

#ifndef KALMAN5_H
#define KALMAN5_H

#define KF_N 5   /* state dimension */
#define KF_M 3   /* measurement dimension */

typedef struct {
    float x[KF_N];           /* state estimate */
    float P[KF_N][KF_N];     /* error covariance */
    float Q[KF_N][KF_N];     /* process noise covariance */
    float R[KF_M][KF_M];     /* measurement noise covariance */
    float F[KF_N][KF_N];     /* state transition matrix */
    float H[KF_M][KF_N];     /* observation matrix */
    float code_carrier_ratio; /* f_chip / f_carrier (~9.46e-5) */
    float T;                  /* integration interval (seconds) */
    float innov_accum[KF_M];  /* innovation accumulator for R adaptation */
    int   innov_count;
    int   adapt_interval;     /* recalculate R every N epochs */
} kalman5_t;

/**
 * @brief Initialize Kalman filter state and matrices.
 * @param kf      Filter state to initialize.
 * @param T       Integration interval (seconds per epoch).
 * @param fc      Carrier frequency (Hz), for code-carrier coupling.
 * @param f_chip  Chip rate (Hz).
 */
void kalman5_init(kalman5_t *kf, float T, float fc, float f_chip);

/**
 * @brief Predict step: x = F*x, P = F*P*F' + Q.
 */
void kalman5_predict(kalman5_t *kf);

/**
 * @brief Update step with measurement vector.
 * @param z  Measurement [delta_phase, delta_freq, delta_tau].
 */
void kalman5_update(kalman5_t *kf, const float z[KF_M]);

/**
 * @brief Adapt Q and R from innovation statistics (call periodically).
 */
void kalman5_adapt_noise(kalman5_t *kf);

#endif /* KALMAN5_H */
