// Portable Linux builds (-DLM_LINUX_PORTABLE=ON): lets an executable built on a recent distribution run on older
// ones (glibc 2.34 and later). A recent glibc renames strtol/strtoul (C23) and the static C++ library calls
// arc4random (glibc 2.36); these small replacements call the older functions, so the executable does not ask
// for the newer glibc versions.
#include <cstddef>                                 // defines __GLIBC__
#if defined(__linux__) && defined(__GLIBC__)
#include <cstdint>
#include <sys/random.h>

extern "C" {
long lm_old_strtol(const char*, char**, int) __asm__("strtol");
unsigned long lm_old_strtoul(const char*, char**, int) __asm__("strtoul");
long long lm_old_strtoll(const char*, char**, int) __asm__("strtoll");
unsigned long long lm_old_strtoull(const char*, char**, int) __asm__("strtoull");

long __isoc23_strtol(const char* s, char** e, int b) { return lm_old_strtol(s, e, b); }
unsigned long __isoc23_strtoul(const char* s, char** e, int b) { return lm_old_strtoul(s, e, b); }
long long __isoc23_strtoll(const char* s, char** e, int b) { return lm_old_strtoll(s, e, b); }
unsigned long long __isoc23_strtoull(const char* s, char** e, int b) { return lm_old_strtoull(s, e, b); }

uint32_t arc4random(void) {
    uint32_t v = 0;
    while (getrandom(&v, sizeof v, 0) != ssize_t(sizeof v)) {}
    return v;
}
}
#endif
