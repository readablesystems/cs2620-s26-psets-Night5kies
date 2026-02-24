#ifdef _WIN32

#include "getopt_win.h"
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

char* optarg = nullptr;
int optind = 1;
int opterr = 1;
int optopt = 0;

static char* place = const_cast<char*>("");

static int getopt_impl(int argc, char* const argv[], const char* optstring) {
    if (!place || !*place) {
        if (optind >= argc || !argv[optind] || *argv[optind] != '-') return -1;
        if (argv[optind][1] == '\0') return -1;
        if (std::strcmp(argv[optind], "--") == 0) { ++optind; return -1; }
        place = argv[optind] + 1;
    }
    char c = *place++;
    const char* p = std::strchr(optstring, c);
    if (!p) {
        optopt = static_cast<unsigned char>(c);
        return '?';
    }
    if (p[1] == ':') {
        if (*place) {
            optarg = place;
            place = const_cast<char*>("");
            ++optind;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
            place = const_cast<char*>("");
        } else {
            optopt = static_cast<unsigned char>(c);
            return (optstring[0] == ':') ? ':' : '?';
        }
    } else {
        if (!*place) { place = const_cast<char*>(""); ++optind; }
    }
    return c;
}

int getopt(int argc, char* const argv[], const char* optstring) {
    return getopt_impl(argc, argv, optstring);
}

int getopt_long(int argc, char* const argv[], const char* optstring,
                const struct option* longopts, int* longindex) {
    if (optind >= argc || !argv[optind]) return -1;
    if (argv[optind][0] != '-') return -1;
    if (argv[optind][1] == '\0') return -1;
    if (std::strcmp(argv[optind], "--") == 0) { ++optind; return -1; }

    if (argv[optind][0] == '-' && argv[optind][1] == '-') {
        const char* name = argv[optind] + 2;
        const char* eq = std::strchr(name, '=');
        size_t namelen = eq ? static_cast<size_t>(eq - name) : std::strlen(name);
        for (int i = 0; longopts[i].name; ++i) {
            if (std::strlen(longopts[i].name) != namelen) continue;
            if (std::strncmp(longopts[i].name, name, namelen) != 0) continue;
            if (longindex) *longindex = i;
            if (longopts[i].flag) {
                *longopts[i].flag = longopts[i].val;
                ++optind;
                return 0;
            }
            if (longopts[i].has_arg == required_argument) {
                if (eq) { optarg = const_cast<char*>(eq + 1); ++optind; return longopts[i].val; }
                if (optind + 1 < argc) { optarg = argv[optind + 1]; optind += 2; return longopts[i].val; }
                optopt = 0;
                return '?';
            }
            if (longopts[i].has_arg == optional_argument && eq) optarg = const_cast<char*>(eq + 1);
            else optarg = nullptr;
            ++optind;
            return longopts[i].val;
        }
        optopt = 0;
        return '?';
    }
    return getopt_impl(argc, argv, optstring);
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
