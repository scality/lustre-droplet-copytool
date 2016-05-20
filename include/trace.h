#ifndef		_TRACE_H_
# define	_TRACE_H_

# include	<stdio.h>
# include	<string.h>
# include	<stdarg.h>
# include	<errno.h>

# define	RESET_COLOR	"\e[0m"
# define	MAKE_GREEN	"\e[1;32m"
# define	MAKE_RED	"\e[1;31m"
# define	MAKE_YELLOW	"\e[1;33m"

# define	ERR		("ERROR")
# define	WARN		("WARNING")
# define	TRACE		("TRACE")

int		_trace(int, char *, char *, ...);

#endif		/* _TRACE_H_ */
