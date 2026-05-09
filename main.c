#include "include/kernel.h"

int main() {
    kernel_init();

    while (1) {
        kernel_run();
    }
}