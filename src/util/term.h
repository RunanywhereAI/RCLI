/**
 * @file term.h
 * @brief Terminal capabilities: TTY detection, width, color policy.
 */

#ifndef WALLY_UTIL_TERM_H
#define WALLY_UTIL_TERM_H

namespace wally::term {

/** True when stdout is an interactive terminal. */
bool stdout_is_tty();

/** True when stderr is an interactive terminal. */
bool stderr_is_tty();

/** True when stdin is an interactive terminal (REPL gate). */
bool stdin_is_tty();

/** Columns of the controlling terminal (fallback 80). */
int terminal_width();

/** ANSI color allowed on stderr: TTY and NO_COLOR unset. */
bool color_enabled();

}  // namespace wally::term

#endif  // WALLY_UTIL_TERM_H
