#ifndef LIBPATCHELF_H
#define LIBPATCHELF_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int patchelf_get_interpreter(const char *filename, char *interpreter,
                             size_t n, int print_err);

int patchelf_set_interpreter(const char *filename, const char *filename_new,
                             const char *interpreter, int print_err);

int patchelf_set_rpath(const char *filename, const char *filename_new,
                      const char *rpath, int print_err);

#ifdef __cplusplus
}
#endif

#endif /* LIBPATCHELF_H */
