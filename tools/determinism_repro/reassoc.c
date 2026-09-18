#include <stdio.h>
volatile double v[4] = {1e16, 1.0, -1e16, 1.0};
int main(void){
    double s = 0; for (int i=0;i<4;i++) s += v[i];   /* 왼쪽부터 순서대로 */
    printf("합 = %.17g\n", s);
    return 0;
}
