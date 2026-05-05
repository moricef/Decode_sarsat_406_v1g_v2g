/**
 * @file kalman5.c
 * @brief 5-state Kalman filter for joint carrier/code tracking.
 *
 * State: [carrier_phase, carrier_freq, freq_rate, code_phase, code_rate]
 * Implements predict/update with F matrix coupling carrier freq to code rate
 * via f_chip/f_carrier ratio (Zhang et al. eq. 59.7).
 *
 * Initial R values are conservative (high measurement noise assumption);
 * kalman5_adapt_noise() tightens them from innovation statistics.
 */
#include "kalman5.h"
#include <math.h>
#include <string.h>
static void mat_mul_5(float C[KF_N][KF_N],const float A[KF_N][KF_N],const float B[KF_N][KF_N]){for(int i=0;i<KF_N;i++)for(int j=0;j<KF_N;j++){float s=0;for(int k=0;k<KF_N;k++)s+=A[i][k]*B[k][j];C[i][j]=s;}}
static void mat_mul_5_T(float C[KF_N][KF_N],const float A[KF_N][KF_N],const float B[KF_N][KF_N]){for(int i=0;i<KF_N;i++)for(int j=0;j<KF_N;j++){float s=0;for(int k=0;k<KF_N;k++)s+=A[i][k]*B[j][k];C[i][j]=s;}}
static void mat_mul_5x3(float C[KF_N][KF_M],const float A[KF_N][KF_M],const float B[KF_M][KF_M]){for(int i=0;i<KF_N;i++)for(int j=0;j<KF_M;j++){float s=0;for(int k=0;k<KF_M;k++)s+=A[i][k]*B[k][j];C[i][j]=s;}}
static void mat_mul_P_Ht(float K[KF_N][KF_M],const float P[KF_N][KF_N],const float H[KF_M][KF_N]){for(int i=0;i<KF_N;i++)for(int j=0;j<KF_M;j++){float s=0;for(int k=0;k<KF_N;k++)s+=P[i][k]*H[j][k];K[i][j]=s;}}
static void mat_mul_H_PHt(float S[KF_M][KF_M],const float H[KF_M][KF_N],const float PHt[KF_N][KF_M]){for(int i=0;i<KF_M;i++)for(int j=0;j<KF_M;j++){float s=0;for(int k=0;k<KF_N;k++)s+=H[i][k]*PHt[k][j];S[i][j]=s;}}
static int mat_inv_3(float I[KF_M][KF_M],const float A[KF_M][KF_M]){float a=A[0][0],b=A[0][1],c=A[0][2],d=A[1][0],e=A[1][1],f=A[1][2],g=A[2][0],h=A[2][1],i=A[2][2];float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(fabsf(det)<1e-30f)return-1;float inv=1.0f/det;I[0][0]=(e*i-f*h)*inv;I[0][1]=(c*h-b*i)*inv;I[0][2]=(b*f-c*e)*inv;I[1][0]=(f*g-d*i)*inv;I[1][1]=(a*i-c*g)*inv;I[1][2]=(c*d-a*f)*inv;I[2][0]=(d*h-e*g)*inv;I[2][1]=(b*g-a*h)*inv;I[2][2]=(a*e-b*d)*inv;return 0;}
static void kf_correct(float x[KF_N],const float K[KF_N][KF_M],const float z[KF_M],const float H[KF_M][KF_N]){float innov[KF_M];for(int j=0;j<KF_M;j++){float s=0;for(int k=0;k<KF_N;k++)s+=H[j][k]*x[k];innov[j]=z[j]-s;}for(int i=0;i<KF_N;i++){float s=0;for(int k=0;k<KF_M;k++)s+=K[i][k]*innov[k];x[i]+=s;}}
static void kf_update_P(float P[KF_N][KF_N],const float K[KF_N][KF_M],const float H[KF_M][KF_N]){float IK[KF_N][KF_N];for(int i=0;i<KF_N;i++)for(int j=0;j<KF_N;j++){float s=0;for(int k=0;k<KF_M;k++)s+=K[i][k]*H[k][j];IK[i][j]=(i==j?1.0f:0.0f)-s;}float Pn[KF_N][KF_N];mat_mul_5(Pn,IK,P);memcpy(P,Pn,sizeof(Pn));}

void kalman5_init(kalman5_t*kf,float T,float fc,float f_chip){
	memset(kf,0,sizeof(*kf));
	kf->T=T;
	kf->code_carrier_ratio=f_chip/fc;
	kf->adapt_interval=20;
	float T2=T*T/2;
	float r=kf->code_carrier_ratio;
	kf->F[0][0]=1;kf->F[0][1]=T;kf->F[0][2]=T2;
	kf->F[1][1]=1;kf->F[1][2]=T;
	kf->F[2][2]=1;
	kf->F[3][3]=1;kf->F[3][4]=T;
	kf->F[4][1]=-r;kf->F[4][4]=1;
	kf->H[0][0]=1;kf->H[1][1]=1;kf->H[2][3]=1;
	/* Process noise (Zhang et al. defaults, scaled by T) */
	kf->Q[0][0]=0.01f*T;kf->Q[1][1]=1.0f*T;kf->Q[2][2]=10.0f*T;
	kf->Q[3][3]=1e-4f*T;kf->Q[4][4]=1e-6f*T;
	/* Initial covariance: large uncertainty on freq/freq-rate */
	kf->P[0][0]=1;kf->P[1][1]=100;kf->P[2][2]=1000;
	kf->P[3][3]=1e-2f;kf->P[4][4]=1e-4f;
	/* Conservative initial R: assume noisy measurements.
	 * Phase: d_phase is disabled → R[0][0] set high so Kalman ignores it.
	 * Freq:  d_freq std ~100 Hz on noise → R[1][1] = 100² = 10000.
	 * Code:  d_tau std ~0.3 chip on noise → R[2][2] = 0.1. */
	kf->R[0][0]=100.0f;kf->R[1][1]=10000.0f;kf->R[2][2]=0.1f;
}

void kalman5_predict(kalman5_t*kf){
	float xn[KF_N];
	for(int i=0;i<KF_N;i++){
		float s=0;
		for(int k=0;k<KF_N;k++)s+=kf->F[i][k]*kf->x[k];
		xn[i]=s;
	}
	memcpy(kf->x,xn,sizeof(xn));
	float FP[KF_N][KF_N];
	mat_mul_5(FP,kf->F,kf->P);
	mat_mul_5_T(kf->P,FP,kf->F);
	for(int i=0;i<KF_N;i++)for(int j=0;j<KF_N;j++)kf->P[i][j]+=kf->Q[i][j];
}

void kalman5_update(kalman5_t*kf,const float z[KF_M]){
	/* Accumulate innovation statistics for R adaptation */
	float innov[KF_M];
	for(int j=0;j<KF_M;j++){
		float s=0;
		for(int k=0;k<KF_N;k++)s+=kf->H[j][k]*kf->x[k];
		innov[j]=z[j]-s;
	}
	/* Outlier rejection: skip update if any innovation > 6 sigma */
	int reject=0;
	for(int i=0;i<KF_M;i++){
		float sigma=sqrtf(kf->R[i][i]);
		if(sigma>0&&fabsf(innov[i])>6.0f*sigma)reject=1;
	}
	if(reject)return;

	for(int i=0;i<KF_M;i++){
		kf->innov_accum[i]+=innov[i];
		kf->innov_sq_accum[i]+=innov[i]*innov[i];
	}
	kf->innov_count++;

	float PHt[KF_N][KF_M];mat_mul_P_Ht(PHt,kf->P,kf->H);
	float S[KF_M][KF_M];mat_mul_H_PHt(S,kf->H,PHt);
	for(int i=0;i<KF_M;i++)for(int j=0;j<KF_M;j++)S[i][j]+=kf->R[i][j];
	float Si[KF_M][KF_M];
	if(mat_inv_3(Si,S)!=0)return;
	float K[KF_N][KF_M];mat_mul_5x3(K,PHt,Si);
	kf_correct(kf->x,K,z,kf->H);
	kf_update_P(kf->P,K,kf->H);
}

void kalman5_adapt_noise(kalman5_t*kf){
	if(kf->innov_count<kf->adapt_interval)return;
	for(int i=0;i<KF_M;i++){
		float mean=kf->innov_accum[i]/(float)kf->innov_count;
		/* variance = E[x²] - E[x]² */
		float var=kf->innov_sq_accum[i]/(float)kf->innov_count - mean*mean;
		if(var<1e-12f)var=1e-12f; /* numerical floor */
		kf->R[i][i]=0.9f*kf->R[i][i]+0.1f*var;
	}
	memset(kf->innov_accum,0,sizeof(kf->innov_accum));
	memset(kf->innov_sq_accum,0,sizeof(kf->innov_sq_accum));
	kf->innov_count=0;
}
