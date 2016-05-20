#include	<stdlib.h>
#include	<string.h>
#include	<time.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/stat.h>
#include	"trace.h"

void		_type(int c, char *type)
{
  if ((strcmp(type, ERR)) == 0)
    {
      fprintf((c == 1 ? stdout : stderr),
	      MAKE_RED"[%s]"RESET_COLOR" : ", type);
      fflush((c == 1 ? stdout : stderr));
    }
  else if ((strcmp(type, WARN)) == 0)
    {
      fprintf((c == 1 ? stdout : stderr),
	      MAKE_YELLOW"[%s]"RESET_COLOR" : ", type);
      fflush((c == 1 ? stdout : stderr));
    }
  else
    {
      fprintf((c == 1 ? stdout : stderr),
	      MAKE_GREEN"[%s]"RESET_COLOR" : ", type);
      fflush((c == 1 ? stdout : stderr));
    }
}

int		_trace(int err, char *type, char *fmt, ...)
{
  va_list	args;
  time_t	curr_time;
  char		*c_time_str;

  curr_time = time(NULL);
  c_time_str = ctime(&curr_time);
  c_time_str[strlen(c_time_str) - 1] = 0;
  fprintf((!strcmp(type, ERR) ? stderr :
	   (!strcmp(type, WARN) ? stderr : stdout)), "[%s] ", c_time_str);
  fflush((!strcmp(type, ERR) ? stderr :
	  (!strcmp(type, WARN) ? stderr : stdout)));
  _type((!strcmp(type, ERR) ? 2 :
	 (!strcmp(type, WARN) ? 2 : 1)), type);
  va_start(args, fmt);
  vfprintf((!strcmp(type, ERR) ? stderr :
	    (!strcmp(type, WARN) ? stderr : stdout)), fmt, args);
  va_end(args);
  if (errno != 0)
    {
      fprintf((!strcmp(type, ERR) ? stderr :
	       (!strcmp(type, WARN) ? stderr : stdout)), " : ");
      fflush((!strcmp(type, ERR) ? stderr :
	      (!strcmp(type, WARN) ? stderr : stdout)));
      fprintf((!strcmp(type, ERR) ? stderr :
	       (!strcmp(type, WARN) ? stderr : stdout)), MAKE_RED);
      perror("");
      fprintf((!strcmp(type, ERR) ? stderr :
	       (!strcmp(type, WARN) ? stderr : stdout)), RESET_COLOR);
    }
  else
    fprintf((!strcmp(type, ERR) ? stderr :
	     (!strcmp(type, WARN) ? stderr : stdout)), "\n");
  return (err);
}
