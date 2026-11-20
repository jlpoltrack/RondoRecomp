/* RondoRecomp.app's Contents/MacOS/RondoRecomp, as the AppImage's tools/linux/AppRun: sets
 * RONDO_APPDIR and RONDO_HOME, then starts the game, Setup (--setup) or rondo_setup (--setup-cli).
 * See docs/distribution.md. */
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char here[PATH_MAX], home[PATH_MAX];

/* why: NULL for errno's message */
static void fail(const char *what, const char *path, const char *why)
{
    fprintf(stderr, "RondoRecomp: %s %s: %s\n", what, path, why ? why : strerror(errno));
    exit(1);
}

static void make_dirs(const char *path)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    if (mkdir(p, 0755) != 0 && errno != EEXIST) fail("cannot make", p, NULL);
}

/* the whole of a small file, "" if there is none */
static void read_text(const char *path, char *out, size_t n)
{
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t got = fread(out, 1, n - 1, f);
    out[got] = 0;
    fclose(f);
}

/* program (a name in here, or a full path) with args[0..argc), and extra before them */
static void run(const char *program, const char *extra, int argc, char **args)
{
    char path[PATH_MAX];
    if (program[0] == '/') snprintf(path, sizeof path, "%s", program);
    else snprintf(path, sizeof path, "%s%s", here, program);
    char **argv = calloc((size_t)argc + 3, sizeof *argv);
    int n = 0;
    argv[n++] = path;
    if (extra) argv[n++] = (char *)extra;
    for (int i = 0; i < argc; i++) argv[n++] = args[i];
    execv(path, argv);
    fail("cannot start", path, NULL);
}

int main(int argc, char **argv)
{
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size) != 0 || !realpath(exe, real)) fail("cannot find", "itself", NULL);
    snprintf(here, sizeof here, "%s", real);
    *(strrchr(here, '/') + 1) = 0;   /* Contents/MacOS/ */

    const char *h = getenv("RONDO_HOME"), *user = getenv("HOME");
    if (h && *h) snprintf(home, sizeof home, "%s", h);
    else if (user && *user) snprintf(home, sizeof home, "%s/Library/Application Support/RondoRecomp/install", user);
    else fail("no folder for", "the game", "HOME is not set");
    size_t l = strlen(home);
    while (l > 1 && home[l - 1] == '/') home[--l] = 0;
    make_dirs(home);
    snprintf(home + l, sizeof home - l, "/");
    setenv("RONDO_APPDIR", here, 1);
    setenv("RONDO_HOME", home, 1);

    if (argc > 1 && !strcmp(argv[1], "--setup")) run("RondoRecomp Setup", NULL, argc - 2, argv + 2);
    if (argc > 1 && !strcmp(argv[1], "--setup-cli")) run("rondo_setup", NULL, argc - 2, argv + 2);

    char game[PATH_MAX], built[PATH_MAX], mine[PATH_MAX], a[256], b[256];
    snprintf(game, sizeof game, "%srondo", home);
    if (access(game, X_OK) == 0) {
        snprintf(built, sizeof built, "%sbuild/version.txt", home);
        snprintf(mine, sizeof mine, "%sversion.txt", here);
        read_text(built, a, sizeof a);
        read_text(mine, b, sizeof b);
        if (a[0] && !strcmp(a, b)) run(game, NULL, argc - 1, argv + 1);
        /* built by another version (the app was replaced): rebuild, then play */
        struct stat st;
        snprintf(built, sizeof built, "%sgame", home);
        if (stat(built, &st) == 0 && S_ISDIR(st.st_mode)) run("RondoRecomp Setup", "--rebuild", 0, NULL);
    }
    run("RondoRecomp Setup", NULL, 0, NULL);
    return 1;
}
