/*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <libfastjson/json.h>
#include <libpatchelf/libpatchelf.h>

#define E(...) \
    E_(__FILE__, __FUNCTION__, __LINE__, errno, __VA_ARGS__)

#define INOBUFLEN (sizeof(struct inotify_event) + NAME_MAX + 1)

#define BAKEXT ".patchbak"
#define TMPEXT ".patchtmp"

#if defined(__i386__)
#   define LIBDIR "/lib"
#   define INTERP "ld-linux.so.2"
#elif defined(__x86_64__)
#   define LIBDIR "/lib64"
#   define INTERP "ld-linux-x86-64.so.2"
#elif defined(__arm__)
#   define LIBDIR "/lib"
#   define INTERP "ld-linux-armhf.so.3"
#elif defined(__aarch64__)
#   define LIBDIR "/lib"
#   define INTERP "ld-linux-aarch64.so.1"
#else
#   error "Unsupported CPU architecture"
#endif

static int opt_patch_now = 0;
static FILE *logfp = NULL;
static const char *glibc_interp = LIBDIR "/" INTERP;
static char glibc_interp_new[PATH_MAX];
static char serverdir_path[PATH_MAX];
static char extdir_path[PATH_MAX];
static char extjson_path[PATH_MAX];
static char realcli_path[PATH_MAX];
static char patchlog_path[PATH_MAX];


static void E_(const char *filename, const char *funcname, long line,
               int errnum, const char *fmt, ...)
{
    va_list args;
    time_t t = time(NULL);
    char *errmsg, *stime = ctime(&t);
    FILE *fp = logfp ? logfp : stderr;

    va_start(args, fmt);

    if (stime) {
        stime[strlen(stime)-1] = '\0';
        fprintf(fp, "%s ", stime);
    }

    fprintf(fp, "[%s:%s():%lu] ", filename, funcname, line);
    vfprintf(fp, fmt, args);

    if (errnum && (errmsg = strerror(errnum))) {
        fprintf(fp, ": %s", errmsg);
    }

    fputc('\n', fp);
    fflush(fp);
    errno = 0;
    va_end(args);
}


static int split_path(const char *path, char *dname, char *bname) {
    int ret = -1;
    size_t path_len;
    char tmp[PATH_MAX], *dname_, *bname_;

    if ((path_len = strlen(path)) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("strlen(): %s", path);
        goto end;
    }

    memcpy(tmp, path, path_len + 1);

    if (!(dname_ = dirname(tmp))) {
        E("dirname()", tmp);
        goto end;
    }

    memcpy(dname, dname_, strlen(dname_) + 1);

    memcpy(tmp, path, path_len + 1);

    if (!(bname_ = basename(tmp))) {
        E("basename()", tmp);
        goto end;
    }

    memcpy(bname, bname_, strlen(bname_) + 1);

    ret = 0;

end:
    return ret;
}


static int str_ends_with(const char *s, const char *sfx) {
    int sl = strlen(s), sfxl = strlen(sfx);
    return sl >= sfxl && !strcmp(s + (sl - sfxl), sfx);
}


static int setup_paths(void)
{
    static const char *selfexe_link = "/proc/self/exe";
    int ret = -1;
    ssize_t siz;
    char ch, commit_id[41] = {0}, selfexe[PATH_MAX] = {0};
    char dname[PATH_MAX], fname[PATH_MAX];

    siz = readlink(selfexe_link, selfexe, PATH_MAX - 1);
    if (siz < 0) {
        E("readlink(): %s", selfexe_link);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("readlink(): %s", selfexe_link);
        goto end;
    }

    if (split_path(selfexe, dname, fname) < 0) {
        E("split_path()");
        goto end;
    }

    if (sscanf(fname, "code-%40[0-9a-f]%c", commit_id, &ch) != 1) {
        errno = EINVAL;
        E("error: %s: unknown commit_id", fname);
        goto end;
    }

    siz = snprintf(serverdir_path, PATH_MAX,
                   "%s/cli/servers/Stable-%s/server", dname, commit_id);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    siz = snprintf(extdir_path, PATH_MAX, "%s/extensions", dname);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    siz = snprintf(extjson_path, PATH_MAX, "%s/extensions/extensions.json",
                   dname);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    siz = snprintf(glibc_interp_new, PATH_MAX, "%s/gnu/%s", dname, INTERP);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    siz = snprintf(realcli_path, PATH_MAX, "%s/%s-cli", dname, fname);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    siz = snprintf(patchlog_path, PATH_MAX, "%s/patch.log", dname);
    if (siz < 0) {
        E("snprintf(): %s", dname);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", dname);
        goto end;
    }

    ret = 0;

end:
    return ret;
}


static int setup_logfp(void)
{
    int ret = -1;

    if ((logfp = fopen(patchlog_path, "a")) == NULL) {
        E("fopen(): %s", patchlog_path);
        goto end;
    }

    ret = 0;

end:
    return ret;
}


static int patch_file(const char *fpath, const struct stat *sb)
{
    static const char elfmagic[] = {'\x7f', 'E', 'L', 'F'};

    char interpreter[PATH_MAX], tmppath[PATH_MAX], bakpath[PATH_MAX];
    char dname[PATH_MAX], fname[PATH_MAX];
    char buff[4] = {0};
    int ret = -1, fd = -1, siz;

    if (str_ends_with(fpath, BAKEXT) || str_ends_with(fpath, TMPEXT)) {
        // backed up file or temp file
        ret = 1;
        goto end;
    }

    if (!S_ISREG(sb->st_mode) || sb->st_size <= 4) {
        // cannot be a regular ELF file
        ret = 1;
        goto end;
    }

    if ((fd = open(fpath, O_RDONLY)) < 0) {
        E("open(): %s", fpath);
        goto end;
    }

    if (read(fd, buff, 4) <= 0) {
        E("read(): %s", fpath);
        goto end;
    }

    close(fd);
    fd = -1;

    if (memcmp(buff, elfmagic, 4) != 0) {
        // not an ELF file
        ret = 1;
        goto end;
    }

    if (split_path(fpath, dname, fname) < 0) {
        E("split_path()");
        goto end;
    }

    siz = snprintf(tmppath, PATH_MAX, "%s/.%s%s", dname, fname, TMPEXT);
    if (siz < 0) {
        E("snprintf(): %s", fpath);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", fpath);
        goto end;
    }

    siz = snprintf(bakpath, PATH_MAX, "%s/.%s%s", dname, fname, BAKEXT);
    if (siz < 0) {
        E("snprintf(): %s", fpath);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", fpath);
        goto end;
    }

    // restore backed up file if exists
    if (access(bakpath, F_OK) == 0) {
        if (rename(bakpath, fpath) < 0) {
            E("rename(): %s => %s", bakpath, fpath);
            goto end;
        }
    }

    if (patchelf_get_interpreter(fpath, interpreter, PATH_MAX, FALSE) < 0) {
        // may be a static binary
        ret = 1;
        goto end;
    }

    if (strncmp(glibc_interp, interpreter, PATH_MAX) != 0) {
        // same interpreter
        ret = 1;
        goto end;
    }

    errno = 0;
    E("Patching %s ...", fpath);

    if (patchelf_set_interpreter(fpath, tmppath, glibc_interp_new, TRUE)) {
        goto end;
    }

    if (rename(fpath, bakpath) < 0) {
        E("rename(): %s => %s", fpath, bakpath);
        goto end;
    }

    if (rename(tmppath, fpath) < 0) {
        E("rename(): %s => %s", tmppath, fpath);
        goto end;
    }

    ret = 0;

end:
    if (fd >= 0) {
        close(fd);
    }

    return ret;
}


static inline int patch_file_ftw(const char *fpath, const struct stat *sb,
                                 int, struct FTW *)
{
    patch_file(fpath, sb);
    return 0;
}


static int check_patched(const char *path)
{
    int ret = -1, fd = -1, siz;
    char abspath[PATH_MAX], patched[PATH_MAX], path_prev[PATH_MAX] = {0};
    char dname[PATH_MAX], fname[PATH_MAX];

    if (opt_patch_now) {
        goto end;
    }

    if (!realpath(path, abspath)) {
        E("realpath(): %s", path);
        goto end;
    }

    if (split_path(path, dname, fname) < 0) {
        E("split_path()");
        goto end;
    }

    siz = snprintf(patched, PATH_MAX, "%s/.%s.patched", dname, fname);
    if (siz < 0) {
        E("snprintf(): %s", path);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", path);
        goto end;
    }

    if ((fd = open(patched, O_RDONLY)) < 0) {
        if (errno != ENOENT) {
            E("open(): %s", patched);
        }
        goto end;
    }

    if (read(fd, path_prev, PATH_MAX) <= 0) {
        E("read(): %s", patched);
        goto end;
    }

    if (strncmp(abspath, path_prev, PATH_MAX) != 0) {
        goto end;
    }

    ret = 0;

end:
    if (fd >= 0) {
        close(fd);
    }

    return ret;
}


static int set_patched(const char *path)
{
    int ret = -1, fd = -1, siz;
    ssize_t abspath_size;
    char abspath[PATH_MAX], patched[PATH_MAX];
    char dname[PATH_MAX], fname[PATH_MAX];

    if (!realpath(path, abspath)) {
        E("realpath(): %s", path);
        goto end;
    }

    abspath_size = strlen(abspath);

    if (split_path(path, dname, fname) < 0) {
        E("split_path()");
        goto end;
    }

    siz = snprintf(patched, PATH_MAX, "%s/.%s.patched", dname, fname);
    if (siz < 0) {
        E("snprintf(): %s", path);
        goto end;
    } else if (siz >= PATH_MAX) {
        errno = ENAMETOOLONG;
        E("snprintf(): %s", path);
        goto end;
    }

    if ((fd = open(patched, O_CREAT | O_TRUNC | O_WRONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
        E("open(): %s", patched);
        goto end;
    }

    if (write(fd, abspath, abspath_size) != abspath_size) {
        E("write(): %s", patched);
        goto end;
    }

    ret = 0;

end:
    if (fd >= 0) {
        close(fd);
    }

    return ret;
}


static int patch_dir(const char *dirpath)
{
    int ret = -1;

    if (check_patched(dirpath) == 0) {
        ret = 0;
        goto end;
    }

    if (nftw(dirpath, patch_file_ftw, 20, FTW_PHYS) < 0) {
        E("nftw(): %s", dirpath);
        goto end;
    }

    if (set_patched(dirpath) < 0) {
        goto end;
    }

    ret = 0;

end:
    return ret;
}


static int patch_cli(char *cli_path)
{
    struct stat sb;
    int ret = -1;

    if (check_patched(cli_path) == 0) {
        ret = 0;
        goto end;
    }

    if (stat(cli_path, &sb) < 0) {
        E("stat(): %s", cli_path);
        goto end;
    }

    if (patch_file(cli_path, &sb) < 0) {
        goto end;
    }

    if (set_patched(cli_path) < 0) {
        goto end;
    }

    ret = 0;

end:
    return ret;
}


static int patch_extensions(char *extjson_path)
{
    int ret = -1, fd = -1, n_ext, i;
    struct stat sb;
    char *json_buff = NULL;
    struct fjson_object *parsed_json = NULL;

    if (stat(extdir_path, &sb) < 0 &&
            mkdir(extdir_path, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
        E("mkdir(): %s", extdir_path);
        goto end;
    }

    if ((fd = open(extjson_path, O_CREAT | O_RDONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
        E("open(): %s", extjson_path);
        goto end;
    }

    if (fstat(fd, &sb) == -1) {
        E("fstat(): %s", extjson_path);
        goto end;
    }

    if (!sb.st_size) {
        // skip empty file
        ret = 0;
        goto end;
    }

    if ((json_buff = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0))
            == MAP_FAILED) {
        E("mmap(): %s", extjson_path);
        goto end;
    }

    if (!(parsed_json = fjson_tokener_parse(json_buff))) {
        E("fjson_tokener_parse(): invalid JSON");
        goto end;
    }

    n_ext = fjson_object_array_length(parsed_json);

    for (i = 0; i < n_ext; i++) {
        const char *dirpath;
        struct fjson_object *extension, *location, *path;

        if (!(extension = fjson_object_array_get_idx(parsed_json, i))) {
            continue;
        }

        if (!fjson_object_object_get_ex(extension, "location", &location)) {
            continue;
        }

        if (!fjson_object_object_get_ex(location, "path", &path)) {
            continue;
        }

        if (!(dirpath = fjson_object_get_string(path))) {
            continue;
        }
        patch_dir(dirpath);
    }

    ret = 0;

end:
    if (parsed_json) {
        fjson_object_put(parsed_json);
    }

    if (json_buff) {
        munmap(json_buff, sb.st_size);
    }

    if (fd >= 0) {
        close(fd);
    }

    return ret;
}


static int create_skip_check_file(void)
{
    static const char *path = "/tmp/vscode-skip-server-requirements-check";
    int ret = -1, fd = -1;

    if ((fd = open(path, O_CREAT | O_RDONLY,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
        E("open(): %s", path);
        goto end;
    }

    ret = 0;

end:
    if (fd >= 0) {
        close(fd);
    }

    return ret;
}


static int monitor_loop(int pipe_fd)
{
    struct epoll_event epoll_ev = {0};
    int ret = -1, epoll_fd = -1, inotify_fd = -1, inotify_wd = -1;

    if ((epoll_fd = epoll_create1(0)) < 0) {
        E("epoll_create1()");
        goto end;
    }

    if ((inotify_fd = inotify_init()) < 0) {
        E("inotify_init()");
        goto end;
    }

    if ((inotify_wd = inotify_add_watch(inotify_fd, extjson_path,
                                        IN_CLOSE_WRITE)) < 0) {
        E("inotify_add_watch()");
        goto end;
    }

    epoll_ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    epoll_ev.data.fd = inotify_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &epoll_ev) == -1) {
        E("epoll_ctl()");
        goto end;
    }

    epoll_ev.events = EPOLLHUP | EPOLLERR;
    epoll_ev.data.fd = pipe_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pipe_fd, &epoll_ev) == -1) {
        E("epoll_ctl()");
        goto end;
    }

    for (;;) {
        struct epoll_event events[2];
        char buf[INOBUFLEN];
        int i, nfds;

        if ((nfds = epoll_wait(epoll_fd, events, 2, -1)) < 0) {
            E("epoll_wait()");
            goto end;
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == pipe_fd && events[0].events & EPOLLHUP) {
                // parent exits
                ret = 0;
                goto end;
            }

            if (events[0].events & EPOLLHUP || events[0].events & EPOLLERR) {
                E("epoll()");
                goto end;
            }

            if (events[i].data.fd == inotify_fd &&
                events[0].events & EPOLLIN) {
                read(inotify_fd, buf, INOBUFLEN);
                sleep(5);
                patch_extensions(extjson_path);
            }
        }
    }

end:
    if (inotify_wd >= 0 && inotify_fd >= 0) {
        inotify_rm_watch(inotify_fd, inotify_wd);
    }

    if (inotify_fd >= 0) {
        close(inotify_fd);
    }

    if (pipe_fd >= 0) {
        close(pipe_fd);
    }

    if (epoll_fd >= 0) {
        close(epoll_fd);
    }

    return ret;
}


static int patch_now(void)
{
    pid_t child;
    int status, exitcode;
    char *argv[] = {realcli_path, "--version", 0};

    if (patch_cli(realcli_path) < 0) {
        return EXIT_FAILURE;
    }

    if (patch_dir(serverdir_path) < 0) {
        return EXIT_FAILURE;
    }

    if (patch_extensions(extjson_path) < 0) {
        return EXIT_FAILURE;
    }

    if (create_skip_check_file() < 0) {
        return EXIT_FAILURE;
    }

    child = fork();
    if (child < 0) {
        E("fork()");
        return EXIT_FAILURE;
    } else if (child == 0) {
        execv(realcli_path, argv);
        E("execv(): %s", realcli_path);
        _exit(EXIT_FAILURE);
    }

    if (waitpid(child, &status, 0) < 0) {
        E("waitpid()");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        exitcode = WEXITSTATUS(status);
        if (exitcode != 0) {
            errno = 0;
            E("Failure: cli failed with exit code %d", exitcode);
        }
    } else if (WIFSIGNALED(status)) {
        errno = 0;
        E("Failure: cli exited with signal %d", WTERMSIG(status));
    } else {
        errno = 0;
        E("Failure: cli waitpid() status %d", status);
    }

    errno = 0;
    E("Success");
    return EXIT_SUCCESS;
}


int main(int argc, char **argv)
{
    int i;
    int pipefd[2];
    pid_t child;

    if (setup_paths()) {
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "--patch-now") == 0) {
        opt_patch_now = 1;
        return patch_now();
    }

    if (setup_logfp()) {
        return EXIT_FAILURE;
    }

    for (i = 0; i < argc; i++) {
        errno = 0;
        E("ARG[%d] = %s", i, argv[i]);
    }

    if (patch_cli(realcli_path) < 0) {
        return EXIT_FAILURE;
    }

    if (patch_dir(serverdir_path) < 0) {
        return EXIT_FAILURE;
    }

    if (patch_extensions(extjson_path) < 0) {
        return EXIT_FAILURE;
    }

    if (create_skip_check_file() < 0) {
        return EXIT_FAILURE;
    }

    if (pipe(pipefd) == -1) {
        E("pipe()");
        return EXIT_FAILURE;
    }

    child = fork();

    if (child < 0) {
        E("fork()");
        return EXIT_FAILURE;
    } else if (child == 0) {
        close(pipefd[1]);
        return monitor_loop(pipefd[0]);
    }

    close(pipefd[0]);
    execv(realcli_path, argv);
    E("execv(): %s", realcli_path);
    _exit(EXIT_FAILURE);
}
