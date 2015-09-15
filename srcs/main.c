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
  cpt_opt->is_verbose = 0;
  cpt_opt->arch_ind_count = 0;
  cpt_opt->ring_mp = NULL;
  cpt_opt->lustre_mp = NULL;
}

/*
** Opt_get.
*/

int		get_opt(int ac, char **av, t_opt *cpt_opt)
{
  int		trig;
  struct option	long_opts[] = {
    {"archive",	required_argument,	NULL,			'A'},
    {"daemon",	no_argument,		&cpt_opt->is_daemon,	1},
    {"ring",	required_argument,	0,			'r'},
    {"help",	no_argument,		0,			'h'},
    {"verbose",	no_argument,		0,			'v'},
    {0, 0, 0, 0}
  };
  int		opt_ind = 0;

  optind = 0;
  while ((trig = getopt_long(ac, av, "A:r:hv", long_opts, &opt_ind)) != -1)
    switch (trig)
      {
      case 'A':
	if ((cpt_opt->arch_ind_count >= MAX_ARCH)
	    || ((unsigned int)atoi(optarg) >= MAX_ARCH))
	  {
	    fprintf(stdout, "Archive number must be less"
		    "than %zu.\n", MAX_ARCH);
	    return (-E2BIG);
	  }
	cpt_opt->arch_ind[cpt_opt->arch_ind_count++] = atoi(optarg);
	if (cpt_opt->arch_ind_count <= MAX_ARCH)
	  cpt_opt->arch_ind[cpt_opt->arch_ind_count] = 0;
	break;
      case 'r':
	cpt_opt->ring_mp = optarg;
	break;
      case 'h':
	usage();
	return (-EINVAL);
      case 'v':
	cpt_opt->is_verbose = 1;
	break;
      }
  if (ac != optind + 1)
    {
      fprintf(stdout, "No lustre mount point specified.\n");
      return (-EINVAL);
    }
  cpt_opt->lustre_mp = av[optind];
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
	return (-(ret = errno));
      fprintf(stdout, "Daemon created.\n");
    }
  return (1);
}

/*
** Main.
*/

int		main(int ac, char **av)
{
  int		ret;
  t_opt		cpt_opt;

  init_opt(&cpt_opt);
  if ((ret = get_opt(ac, av, &cpt_opt)) < 0)
    return (ret);
  if (cpt_opt.is_daemon)
    if ((ret = (daemonize(&cpt_opt))) < 0)
      return (ret);
  fprintf(stdout, "(dev msg) Values retrieved :\n"
	  "is_daemon : %d\n"
	  "is_verbose : %d\n"
	  "ring_mp : %s\n"
	  "lustre_mp : %s\n",
	  cpt_opt.is_daemon,
	  cpt_opt.is_verbose,
	  cpt_opt.ring_mp,
	  cpt_opt.lustre_mp);
  unsigned int	i;
  for (i = 0; i < cpt_opt.arch_ind_count; i += 1)
    fprintf(stdout, "(dev msg) Archive index retrieved :\n"
	    "index %d\n",
	    cpt_opt.arch_ind[i]);
  return (1);
}
