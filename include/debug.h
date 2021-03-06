/*
 * Copyright (C) 2014  James Smith <james@theta.pw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdio.h>

/* Prints messages to stderr of the form:
 * [TIME/DATE] message
 */
void debug_log(const char *fmt, ...);

#ifdef DEBUG
#define debug(M, ...)	 debug_log("[DEBUG] " M, ##__VA_ARGS__)
#else
#define debug(M, ...)
#endif				/* DEBUG */

#define log_err(M, ...)  do { \
	debug_log("[ERR] " M " (%s:%d)", \
		##__VA_ARGS__, __FILE__, __LINE__); \
	fflush(NULL); \
	} while(0)

#define log_warn(M, ...) debug_log("[WARN] " M " (%s:%d)", ##__VA_ARGS__, \
		__FILE__, __LINE__)

#define log_info(M, ...) debug_log("[INFO] " M " (%s:%d)", ##__VA_ARGS__, \
		__FILE__, __LINE__)

#endif				/* DEBUG_H_ */
