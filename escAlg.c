#include <stdio.h>
#include <math.h>

typedef struct{
  float x1;
  float x2;
  float y1;
  float y2;
  float b0_over_a0;
  float b1_over_a0;
  float b2_over_a0;
  float minus_a1_over_a0;
  float minus_a2_over_a0;
} biquad_t;

void biquad_initLp(biquad_t* self, float /*fc/fs*/f0, float Q){
  float omega0 = 2.0f*M_PI*f0;
  float alpha = sin(omega0)/(2.0f*Q);
  float cosOmega0 = cos(omega0);
  float b0 = (1.0f-cosOmega0)/2.0f;
  float b1 = (1.0f-cosOmega0);
  float b2 = (1.0f-cosOmega0)/2.0f;
  
  float a0 = 1.0f+alpha;
  float a1 = -2.0f*cosOmega0;
  float a2 = 1.0f-alpha;
  // printf("LP: b0=%f b1=%f b2=%f a0=%f a1=%f a2=%f\n", b0, b1, b2, a0, a1, a2);
  self->b0_over_a0 = b0 / a0;
  self->b1_over_a0 = b1 / a0;
  self->b2_over_a0 = b2 / a0;
  
  self->minus_a1_over_a0 = -a1/a0;
  self->minus_a2_over_a0 = -a2/a0;
}

void biquad_initHp(biquad_t* self, float /*fc/fs*/f0, float Q){
  float omega0 = 2.0f*M_PI*f0;
  float alpha = sin(omega0)/(2.0f*Q);
  float cosOmega0 = cos(omega0);
  float b0 = (1.0f+cosOmega0)/2.0f;
  float b1 = -(1.0f+cosOmega0);
  float b2 = (1.0f+cosOmega0)/2.0f;
  
  float a0 = 1.0f+alpha;
  float a1 = -2.0f*cosOmega0;
  float a2 = 1.0f-alpha;
  // printf("HP: b0=%f b1=%f b2=%f a0=%f a1=%f a2=%f\n", b0, b1, b2, a0, a1, a2);

  self->b0_over_a0 = b0 / a0;
  self->b1_over_a0 = b1 / a0;
  self->b2_over_a0 = b2 / a0;
  
  self->minus_a1_over_a0 = -a1/a0;
  self->minus_a2_over_a0 = -a2/a0;
}

void biquad_run(biquad_t* self, const float* dIn, float* dOut, size_t n){
  while (n--){
    float x0 = *(dIn++);
    float y0 = self->b0_over_a0*x0
      + self->b1_over_a0*self->x1
      + self->b2_over_a0*self->x2
      + self->minus_a1_over_a0 * self->y1
      + self->minus_a2_over_a0 * self->y2;
    *(dOut++) = y0;
    
    // update delay lines for next sample
    self->x2 = self->x1;
    self->x1 = x0;
    
    self->y2 = self->y1;
    self->y1 = y0;
  }
}
	       

int main(int argc, const char** argv){
  if (argc < 2){
    fprintf(stderr, "need filename as argument");
    return -1;
  }
  // FILE* f = fopen("slow.txt", "rb");
  FILE* f = fopen(argv[1], "rb");
  int v1, v2;
  float fs_Hz = 500000;
  biquad_t lp;
  biquad_t hp;  
  biquad_initHp(&hp, 10.0/fs_Hz, /*Q*/0.7);
  biquad_initLp(&lp, 1200.0/fs_Hz, /*Q*/0.7);
  while (fscanf(f, "%i\t%i\n", &v1, &v2) == 2){
    //printf("%i\t%i\n", v1, v2);
    float v3 = (float)v2 - (float)v1;
    biquad_run(&lp, /*in*/&v3, /*out*/&v3, /*n*/1);
    biquad_run(&hp, /*in*/&v3, /*out*/&v3, /*n*/1);
    printf("%f\n", v3);
  }
  return 0;
}
