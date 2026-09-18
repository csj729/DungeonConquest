#include <stdio.h>
#include <stdint.h>
volatile double A = 12052.631484985352, B = 2784.7268676757812, C = -13696.111022949219;
/* 같은 계산을 Fixed 20.12로 */
volatile int32_t fa = (int32_t)(12052.631484985352 * 4096);
volatile int32_t fb = (int32_t)(2784.7268676757812 * 4096);
volatile int32_t fc = (int32_t)(-13696.111022949219 * 4096);
int main(void){
    double d = A*B + C;
    uint64_t db; __builtin_memcpy(&db, &d, 8);
    int32_t f = (int32_t)(((int64_t)fa * fb) >> 12) + fc;   /* int64 승격 후 시프트 */
    printf("double = %016llx      Fixed = %08x\n", (unsigned long long)db, (unsigned)f);
    return 0;
}
