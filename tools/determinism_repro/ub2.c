#include <stdio.h>
#include <stdint.h>
int count_until_overflow(int32_t start){
    int n = 0;
    for (int32_t x = start; x + 1 > x; x++) n++;   /* UB: x+1>x 는 항상 참으로 접힐 수 있다 */
    return n;
}
int main(void){ printf("  반복 횟수 = %d\n", count_until_overflow(2147483640)); return 0; }
