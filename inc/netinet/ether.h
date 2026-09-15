#if defined(__linux__)
#include_next <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif
