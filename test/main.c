
/*
./toolchains/rv64ima/bin/riscv64-unknown-elf-gcc -g -static test/main.c -o test/main.out
*/

#include <stdio.h>
#include <stdlib.h>
// #include <time.h>
int main() {
    // srand(time(0));
    printf("hello, world %d\n", 1);
    return 0;
}
