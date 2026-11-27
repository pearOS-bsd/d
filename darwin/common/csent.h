/* The entitlements of a running process, read from the code signature of its executable (for daemons on pearOS, where csops does not report them).
 * Our own file (BSD-2-Clause). */
#ifndef DARWIN_CSENT_H
#define DARWIN_CSENT_H
/* 1 if the XML entitlements of the x86_64 slice of the executable of `pid` have `key` = <true/>, 0 otherwise */
int csent_bool(int pid, const char *key);
/* the signing identifier of the code signature of the executable of `pid` (CodeDirectory, slot 0) into ident; 1 if there is one */
int csent_identifier(int pid, char *ident, unsigned int size);
#endif
