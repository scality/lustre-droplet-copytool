#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include "cpt_opt.h"
#include "cpt_msgs.h"

/*
** Initializing and getting pre-running options for the daemon copytool.
*/

void		init_opt(t_opt *cpt_opt)
{
  cpt_opt->is_daemon = 0;
}

int		get_opt(int ac, char **av, t_opt *cpt_opt)
{
  int		trig;
  struct option	long_opts[] = {
    {"archive",	required_argument,	NULL,			'A'},
    {"daemon",	no_argument,		&cpt_opt->is_daemon,	1},
    {"verbose",	no_argument,		0,			'v'},
    {0, 0, 0, 0}
  };
  int		opt_ind = 0;

  if (ac == 1)
    return(usage());
  while ((trig = getopt_long(ac, av, "A:dv", long_opts, &opt_ind)) != -1)
    switch (trig)
      {
      case 'A':
	fprintf(stdout, "(dev msg) archive.\n");
	break;
      case 'd':
	cpt_opt->is_daemon = 1;
        break;
      case 'v':
	fprintf(stdout, "(dev msg) verbosing.\n");
	break;
      }
  fprintf(stdout, "(dev msg) dameon = %d\n", cpt_opt->is_daemon);
  return (1);
}

/*
** Initializing daemon-mode.
*/

int		daemonize(t_opt *cpt_opt)
{
  int		ret;

  if (cpt_opt->is_daemon)
    {
      if ((ret = daemon(1, 1)) < 0)
	return ((ret = errno));
      fprintf(stdout, "Daemon created.\n");
    }
  return (1);
}

/*
**
*/

int		main(int ac, char **av)
{
  int		ret;
  t_opt		cpt_opt;

  init_opt(&cpt_opt);
  if ((ret = get_opt(ac, av, &cpt_opt)) == EINVAL)
    return (EINVAL);
  if (cpt_opt.is_daemon)
    daemonize(&cpt_opt);
  printf("Construction checked.\n");
  return (1);
}
