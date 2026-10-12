/*
Check for the SDL_WINDOW_DISPLAY selection logic in code/sdl2/sdl2_glimp.c.
Mirrors GLimp_NameContains and the index/name/out-of-range paths. Run with:
    cc -o /tmp/display-pick-test tools/display-pick-test.c && /tmp/display-pick-test
Exit code is the number of failures.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int nameContains(const char *name, const char *needle)
{
    if (!name || !needle) return 0;
    for (; *name; name++) {
        const char *a = name, *b = needle;
        while (*b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static int pick(const char **names, int count, const char *env)
{
    int disp = 0, i, n, picked = 0, numeric = 0;

    if (env) {
        for (n = 0; env[n]; n++) if (env[n] < '0' || env[n] > '9') break;
        numeric = env[n] == '\0';
        if (numeric) {
            disp = atoi(env);
            if (disp >= 0 && disp < count) picked = 1;
        }
    }
    if (!picked) {
        const char *hints[] = { "built", "internal", "panel", "lcd", "lv" };
        int nhints = env ? 1 : 5, h;
        for (i = 0; i < count; i++) {
            for (h = 0; h < nhints; h++) {
                if (nameContains(names[i], env ? env : hints[h])) { disp = i; picked = 1; break; }
            }
            if (picked) break;
        }
    }
    if (!picked) disp = 0;
    return disp;
}

static struct {
    const char **names; int count; const char *env; int want; const char *what;
} cases[] = {
    { (const char *[]){"Color LCD","DELL U2410","LG UltraFine"}, 3, NULL, 0,
      "laptop first, env unset" },
    { (const char *[]){"DELL U2410","LG UltraFine","Built-in Retina Display"}, 3, NULL, 2,
      "laptop third, env unset" },
    { (const char *[]){"DELL U2410","LG UltraFine"}, 2, NULL, 0,
      "no builtin, env unset -> display 0" },
    { (const char *[]){"Color LCD","DELL U2410","LG UltraFine"}, 3, "1", 1,
      "env '1' is an index" },
    { (const char *[]){"Color LCD","DELL U2410","LG UltraFine"}, 3, "dell", 1,
      "env 'dell' is a name, not atoi 0" },
    { (const char *[]){"Color LCD","DELL U2410","LG UltraFine"}, 3, "UltraFine", 2,
      "env 'UltraFine'" },
    { (const char *[]){"Color LCD","DELL U2410","LG UltraFine"}, 3, "9", 0,
      "env '9' out of range -> display 0" },
    { (const char *[]){"LG UltraFine","Color LCD"}, 2, "lcd", 1,
      "case-insensitive 'lcd'" },
    { (const char *[]){"LG UltraFine","Color LCD"}, 2, "LCD", 1,
      "case-insensitive 'LCD'" },
};

int main(void)
{
    int fails = 0, i;

    for (i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
        int got = pick(cases[i].names, cases[i].count, cases[i].env);
        printf("%-40s -> %d (want %d) %s\n", cases[i].what, got, cases[i].want,
               got == cases[i].want ? "ok" : "FAIL");
        if (got != cases[i].want) fails++;
    }
    printf("\n%d failures\n", fails);
    return fails;
}
