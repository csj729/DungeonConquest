#include <stdio.h>
#include <math.h>
#include <stdint.h>
int main(void){
    uint64_t s = 88172645463325252ull;
    for (long i=0;i<3000000;i++){
        s^=s<<13; s^=s>>7; s^=s<<17;
        double a = (double)(int32_t)(s>>32) / 65536.0;
        s^=s<<13; s^=s>>7; s^=s<<17;
        double b = (double)(int32_t)(s>>32) / 65536.0;
        s^=s<<13; s^=s>>7; s^=s<<17;
        double c = (double)(int32_t)(s>>32) / 65536.0;
        double rounded = a*b + c;          // 곱을 반올림한 뒤 더함
        double fused   = fma(a, b, c);     // 곱을 정확히 두고 한 번만 반올림
        if (rounded != fused){
            printf("a = %.17g\nb = %.17g\nc = %.17g\n", a, b, c);
            printf("a*b + c   = %.17g\nfma(a,b,c) = %.17g\n", rounded, fused);
            printf("차이       = %.3g\n", fused - rounded);
            return 0;
        }
    }
    puts("못 찾음");
    return 1;
}
