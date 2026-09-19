#include <stdio.h>
#define MAX 10

/* Demo program */
int main()
{
    int arr[MAX];
    float pi = 3.14;
    char ch = 'A';
    char *msg = "Hello World\n";   // print message

    if (pi >= 3.0 && ch != 'B')
        arr[0] += 5;

    return 0;
}
