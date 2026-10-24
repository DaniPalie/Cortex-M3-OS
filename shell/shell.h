/*
 * shell.h — Interactive REPL for the batch OS.
 *
 * shell_run() never returns: it loops reading commands from UART and
 * dispatching them. The set of recognised commands is documented in the
 * top comment of shell.c and in the output of the `help` command.
 */

#ifndef OS_SHELL_H
#define OS_SHELL_H

void shell_run(void);

#endif /* OS_SHELL_H */
