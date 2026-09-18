#include <stdio.h>
#include <stdint.h>
volatile double A = 12052.631484985352, B = 2784.7268676757812, C = -13696.111022949219;
int main(void){
    double r = A*B + C;                       /* ← 이 한 줄 */
    uint64_t bits; __builtin_memcpy(&bits, &r, 8);
    printf("r = %.17g   비트 = %016llx\n", r, (unsigned long long)bits);
    return 0;
}
