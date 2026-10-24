/* The tests' one assertion: prints what failed and carries on; main returns check_done() (0: all passed). */
#include <stdio.h>
static int check_fails, check_n;
#define CHECK(cond, ...)                                                                            \
    do {                                                                                            \
        check_n++;                                                                                  \
        if (!(cond)) {                                                                              \
            check_fails++;                                                                          \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                    \
            printf("\n");                                                                           \
        }                                                                                           \
    } while (0)
static int check_done(const char *name)
{
    printf("%s: %d of %d checks passed%s\n", name, check_n - check_fails, check_n, check_fails ? "  <- FAILED" : "");
    return check_fails != 0;
}
