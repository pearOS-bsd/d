/* The entitlements of a running process, read from the code signature of its executable (for daemons on pearOS, where csops does not report them).
 * Our own file (BSD-2-Clause). */
#ifndef DARWIN_CSENT_H
#define DARWIN_CSENT_H
/* 1 if the XML entitlements of the x86_64 slice of the executable of `pid` have `key` = <true/>, 0 otherwise */
int csent_bool(int pid, const char *key);
#endif
