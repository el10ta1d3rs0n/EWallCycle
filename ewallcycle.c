#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define APP "ewallcycle"

struct state {
    char dir[PATH_MAX];
    char cur[PATH_MAX];
    char mode[16];
};

static char cfgdir[PATH_MAX], cfgfile[PATH_MAX], pidfile[PATH_MAX];

static void die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, APP ": ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* bounded copy, always NUL-terminated, truncates on purpose */
static void scopy(char *dst, size_t size, const char *src)
{
    size_t n = strnlen(src, size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* dst = a + b; dies instead of silently truncating a path */
static void join(char *dst, size_t size, const char *a, const char *b)
{
    int n = snprintf(dst, size, "%s%s", a, b);
    if (n < 0 || (size_t)n >= size)
        die("path too long");
}

/* ---------- paths ---------- */

static void init_paths(void)
{
    const char *x = getenv("XDG_CONFIG_HOME");
    const char *h = getenv("HOME");
    const char *r = getenv("XDG_RUNTIME_DIR");
    char base[PATH_MAX];

    if (x && *x) {
        scopy(base, sizeof base, x);
        join(cfgdir, sizeof cfgdir, base, "/" APP);
    } else if (h && *h) {
        scopy(base, sizeof base, h);
        join(cfgdir, sizeof cfgdir, base, "/.config/" APP);
    } else {
        die("neither XDG_CONFIG_HOME nor HOME is set");
    }

    join(cfgfile, sizeof cfgfile, cfgdir, "/state");

    if (r && *r) {
        scopy(base, sizeof base, r);
        join(pidfile, sizeof pidfile, base, "/" APP ".pid");
    } else {
        snprintf(base, sizeof base, "/tmp/" APP "-%u.pid", (unsigned)getuid());
        scopy(pidfile, sizeof pidfile, base);
    }
}

static void mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    scopy(tmp, sizeof tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
        die("cannot create %s: %s", tmp, strerror(errno));
}

/* ---------- state ---------- */

static void load_state(struct state *s)
{
    char line[PATH_MAX + 16];
    FILE *f;

    memset(s, 0, sizeof *s);
    scopy(s->mode, sizeof s->mode, "fill");

    f = fopen(cfgfile, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        if (!strncmp(line, "dir=", 4))
            scopy(s->dir, sizeof s->dir, line + 4);
        else if (!strncmp(line, "current=", 8))
            scopy(s->cur, sizeof s->cur, line + 8);
        else if (!strncmp(line, "mode=", 5))
            scopy(s->mode, sizeof s->mode, line + 5);
    }
    fclose(f);
}

static void save_state(const struct state *s)
{
    char tmp[PATH_MAX + 8];
    FILE *f;

    mkdir_p(cfgdir);
    join(tmp, sizeof tmp, cfgfile, ".tmp");
    f = fopen(tmp, "w");
    if (!f)
        die("cannot write %s: %s", tmp, strerror(errno));
    fprintf(f, "dir=%s\ncurrent=%s\nmode=%s\n", s->dir, s->cur, s->mode);
    if (fclose(f) != 0 || rename(tmp, cfgfile) != 0)
        die("cannot save state: %s", strerror(errno));
}

/* ---------- image listing ---------- */

static int is_image(const char *name)
{
    static const char *ext[] = { ".jpg", ".jpeg", ".png", ".webp", ".bmp", NULL };
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
    for (int i = 0; ext[i]; i++)
        if (!strcasecmp(dot, ext[i]))
            return 1;
    return 0;
}

static int cmp(const void *a, const void *b)
{
    return strverscmp(*(char *const *)a, *(char *const *)b);
}

static char **list_images(const char *dir, size_t *count)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char **v = NULL;
    size_t n = 0, cap = 0;

    if (!d)
        die("cannot open %s: %s", dir, strerror(errno));

    while ((e = readdir(d))) {
        char path[PATH_MAX];
        struct stat st;
        int len;

        if (e->d_name[0] == '.' || !is_image(e->d_name))
            continue;
        len = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (len < 0 || (size_t)len >= sizeof path)
            continue; /* path too long, skip */
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (n == cap) {
            char **nv;
            cap = cap ? cap * 2 : 64;
            nv = realloc(v, cap * sizeof *v);
            if (!nv)
                die("out of memory");
            v = nv;
        }
        v[n] = strdup(path);
        if (!v[n])
            die("out of memory");
        n++;
    }
    closedir(d);

    if (n)
        qsort(v, n, sizeof *v, cmp);
    *count = n;
    return v;
}

static void free_list(char **v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static long find_index(char **v, size_t n, const char *cur)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(v[i], cur))
            return (long)i;
    return -1;
}

/* ---------- swaybg control ---------- */

static pid_t read_pid(void)
{
    FILE *f = fopen(pidfile, "r");
    int pid = 0;
    if (!f)
        return 0;
    if (fscanf(f, "%d", &pid) != 1)
        pid = 0;
    fclose(f);
    return pid > 0 ? (pid_t)pid : 0;
}

static void write_pid(pid_t pid)
{
    FILE *f = fopen(pidfile, "w");
    if (!f)
        return;
    fprintf(f, "%d\n", (int)pid);
    fclose(f);
}

/* guard against killing a recycled PID */
static int is_swaybg(pid_t pid)
{
    char path[64], comm[32] = { 0 };
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
    f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(comm, sizeof comm, f))
        comm[0] = '\0';
    fclose(f);
    comm[strcspn(comm, "\n")] = '\0';
    return !strcmp(comm, "swaybg");
}

static void apply(const struct state *s)
{
    pid_t old = read_pid();
    pid_t pid;

    if (access(s->cur, R_OK) != 0)
        die("cannot read %s", s->cur);

    pid = fork();
    if (pid < 0)
        die("fork failed: %s", strerror(errno));

    if (pid == 0) {
        int fd;
        setsid();
        fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
        execlp("swaybg", "swaybg", "-o", "*", "-i", s->cur,
               "-m", s->mode, (char *)NULL);
        _exit(127);
    }

    write_pid(pid);

    /* let the new surface appear, then drop the old one (no flicker) */
    if (old > 0 && old != pid && is_swaybg(old)) {
        struct timespec ts = { 0, 300 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        kill(old, SIGTERM);
    }
}

/* ---------- commands ---------- */

static void require_dir(const struct state *s)
{
    if (!s->dir[0])
        die("no wallpaper directory set; run: " APP " set-dir <path>");
}

static void cmd_set_dir(const char *arg)
{
    char real[PATH_MAX];
    struct state s;
    struct stat st;
    char **v;
    size_t n;

    if (!realpath(arg, real) || stat(real, &st) != 0 || !S_ISDIR(st.st_mode))
        die("'%s' is not a directory", arg);

    load_state(&s);
    scopy(s.dir, sizeof s.dir, real);

    v = list_images(s.dir, &n);
    if (!n)
        die("no images found in %s", s.dir);
    scopy(s.cur, sizeof s.cur, v[0]);
    free_list(v, n);

    save_state(&s);
    apply(&s);
}

/* dir: +1 next, -1 prev, 0 random */
static void cmd_move(int dir)
{
    struct state s;
    char **v;
    size_t n;
    long idx, ni;

    load_state(&s);
    require_dir(&s);

    v = list_images(s.dir, &n);
    if (!n)
        die("no images found in %s", s.dir);

    idx = find_index(v, n, s.cur);

    if (dir > 0) {
        ni = (idx + 1) % (long)n;
    } else if (dir < 0) {
        ni = idx <= 0 ? (long)n - 1 : idx - 1;
    } else {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        ni = rand() % (long)n;
        if (n > 1 && ni == idx)
            ni = (ni + 1) % (long)n;
    }

    scopy(s.cur, sizeof s.cur, v[ni]);
    free_list(v, n);

    save_state(&s);
    apply(&s);
}

static void cmd_restore(void)
{
    struct state s;

    load_state(&s);
    require_dir(&s);

    if (!s.cur[0] || access(s.cur, R_OK) != 0) {
        /* saved file vanished: fall back to first image in the folder */
        size_t n;
        char **v = list_images(s.dir, &n);
        if (!n)
            die("no images found in %s", s.dir);
        scopy(s.cur, sizeof s.cur, v[0]);
        free_list(v, n);
        save_state(&s);
    }
    apply(&s);
}

static void cmd_mode(const char *m)
{
    static const char *ok[] = { "fill", "fit", "stretch", "center", "tile", NULL };
    struct state s;
    int valid = 0;

    for (int i = 0; ok[i]; i++)
        if (!strcmp(m, ok[i]))
            valid = 1;
    if (!valid)
        die("mode must be one of: fill fit stretch center tile");

    load_state(&s);
    scopy(s.mode, sizeof s.mode, m);
    save_state(&s);
    if (s.cur[0])
        apply(&s);
}

static void usage(void)
{
    puts("usage: " APP " <command>\n"
         "  set-dir <path>   set wallpaper folder and apply first image\n"
         "  next | prev      cycle wallpapers\n"
         "  random           pick a random wallpaper\n"
         "  restore          reapply last wallpaper (use at login)\n"
         "  mode <m>         fill|fit|stretch|center|tile\n"
         "  current          print current wallpaper");
}

int main(int argc, char **argv)
{
    init_paths();

    if (argc < 2) {
        usage();
        return 1;
    }

    if (!strcmp(argv[1], "set-dir") && argc == 3)
        cmd_set_dir(argv[2]);
    else if (!strcmp(argv[1], "next"))
        cmd_move(1);
    else if (!strcmp(argv[1], "prev"))
        cmd_move(-1);
    else if (!strcmp(argv[1], "random"))
        cmd_move(0);
    else if (!strcmp(argv[1], "restore"))
        cmd_restore();
    else if (!strcmp(argv[1], "mode") && argc == 3)
        cmd_mode(argv[2]);
    else if (!strcmp(argv[1], "current")) {
        struct state s;
        load_state(&s);
        if (s.cur[0])
            puts(s.cur);
    } else {
        usage();
        return 1;
    }
    return 0;
}
