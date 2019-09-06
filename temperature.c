#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <linux/i2c-dev.h>
#include "mod_mmio.h"
#include <stdio.h>


#define ID_REG_BASE 0x01c23800

int main(int argc, char **argv) {

    int adj = 1447;
    struct mmio io;

    mmio_write(0x01c25000, 0x0027003f);
    mmio_write(0x01c25010, 0x00040000);
    mmio_write(0x01c25018, 0x00010fff);
    mmio_write(0x01c25004, 0x00000090);


    double temp_cpu = 0;


    temp_cpu = (float) mmio_read(0x01c25020);
    if (temp_cpu < 1) {
        printf("Erro\n");
        return 0;
    }

    if (argc > 1) {
        if (!strcasecmp(argv[1], "-v")) {
            printf("CPU Temp: %0.1f", (float) (temp_cpu - adj) / 10);
            printf("\n");
        }
    }

    FILE *fp = fopen("/data/temperature.dat", "w");
    if (fp != NULL) {

        fprintf(fp, "[temperature]\ncpu=%.2f\n", (float) ((temp_cpu - adj) / 10));
        fclose(fp);
    }

    return 0;

}
