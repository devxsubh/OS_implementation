#include "approx.h"
#include "experiments.h"

#include <stdio.h>
#include <string.h>

static void usage(const char *bin) {
    fprintf(stderr, "Usage: %s --experiment all|1|2|3|4|5\n", bin);
}

int main(int argc, char **argv) {
    init_luts();
    const char *exp = "all";
    if (argc == 3 && strcmp(argv[1], "--experiment") == 0) exp = argv[2];
    else if (argc != 1) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(exp, "all") == 0 || strcmp(exp, "1") == 0) print_experiment_1();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "2") == 0) print_experiment_2();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "3") == 0) print_experiment_3();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "4") == 0) print_experiment_4();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "5") == 0) print_experiment_5();
    return 0;
}
