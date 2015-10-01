#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include "cpt_opt.h"
#include "cpt_msgs.h"
#include <lustre/lustreapi.h>
#include <openssl/bn.h>

#define	KEY_SIZE  159

static struct hsm_copytool_private	*cpt_data;
static char				fs_name[MAX_OBD_NAME + 1];
static int				arch_fd;

typedef struct	s_opt
{
  int		is_daemon;
  int		is_verbose;
  int		arch_ind[MAX_ARCH];
  unsigned int	arch_ind_count;
  char		*ring_mp;
  char		*o_mnt;
  int		o_mnt_fd;
}		t_opt;

/*
** Initializing and getting pre-running options for the daemon copytool.
*/

void		init_opt(t_opt *cpt_opt) {
  cpt_opt->is_daemon = 0;
  cpt_opt->is_verbose = 0;
  cpt_opt->arch_ind_count = 0;
  cpt_opt->ring_mp = NULL;
  cpt_opt->o_mnt = NULL;
  cpt_opt->o_mnt_fd = -1;
  cpt_data = NULL;
}

/*
** Opt_get.
*/

int		get_opt(int ac, char **av, t_opt *cpt_opt) {
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
    switch (trig) {
    case 'A':
      if ((cpt_opt->arch_ind_count >= MAX_ARCH)
	  || ((unsigned int)atoi(optarg) >= MAX_ARCH)) {
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
      return (usage());
    case 'v':
      cpt_opt->is_verbose = 1;
      break;
    }
  if (ac < 4) {
    fprintf(stdout, "Invalid options, try --help or -h for more informations.\n");
    return (-EINVAL);
    }
  if (ac != optind + 1 && optind >= MIN_OPT) {
    fprintf(stdout, "No lustre mount point specified.\n");
    return (-EINVAL);
  }
  cpt_opt->o_mnt = av[optind];
  if (cpt_opt->ring_mp == NULL || cpt_opt->o_mnt == NULL) {
    if (cpt_opt->ring_mp == NULL)
      fprintf(stdout, "Must specify a root directory for the ring.\n");
    if (cpt_opt->o_mnt == NULL)
      fprintf(stdout, "Must specify a root directory for the lustre fs.\n");
    return (-EINVAL);
  }
  return (1);
}

/*
** Initializing daemon-mode.
*/

int		daemonize(t_opt *cpt_opt) {
  int		ret;
  
  if (cpt_opt->is_daemon) {
    if ((ret = daemon(1, 1)) < 0)
      return (-(ret = errno));
    fprintf(stdout, "Daemon created.\n");
  }
  return (1);
}

/*
** sig_hander
*/

static void	sighand(int sig) {
  psignal(sig, "exiting");
  llapi_hsm_copytool_unregister(&cpt_data);
  exit(1);
}

/*
** cpt_functions.
*/

static int	cpt_run(t_opt *cpt_opt) {
  int		ret;

  ret = llapi_hsm_copytool_register(&cpt_data, cpt_opt->o_mnt,
				    cpt_opt->arch_ind_count,
				    cpt_opt->arch_ind, 0);
  if (ret < 0) {
    fprintf(stdout, "Cannot start copytool interface.\n");
    return (ret);
  }
  signal(SIGINT, sighand);
  signal(SIGTERM, sighand);

  while (1) {
    struct hsm_action_list	*hal;
    struct hsm_action_item	*hai;
    BIGNUM			*BN = NULL;
    int				msg_size;
    
    ret = llapi_hsm_copytool_recv(cpt_data, &hal, &msg_size);
    if (ret == -ESHUTDOWN) {
      fprintf(stdout, "Shutting down.\n");
      break;
    }
    fprintf(stdout, "Copytool fs=%s, archive#=%d, item_count=%d.\n",
	    hal->hal_fsname, hal->hal_archive_id, hal->hal_count);
    // check fs_name with strcmp.
    hai = hai_first(hal);
    fprintf(stdout, "Action done : %s\nFID : %lu, %u, %u\n",
	    hsm_copytool_action2name(hai->hai_action), hai->hai_fid.f_seq, hai->hai_fid.f_oid, hai->hai_fid.f_ver);
    if (!(BN = BN_new()))
      return (-ENOMEM);
    BN_set_bit(BN, KEY_SIZE);
    BN_clear_bit(BN, KEY_SIZE);
    dpl_uks_gen_key_raw(BN, (hai->hai_fid.f_ver >> 8), hai->hai_fid.f_seq, hai->hai_fid.f_oid, 0,
			((hai->hai_fid.f_ver & 0xFF) << 16));
    char			*tmp;
    tmp = BN_bn2hex(BN);
    fprintf(stdout, "BIGNUM = %s\n", tmp);
    OPENSSL_free(tmp);
    // Need to figure out if this is the storage. Async func in it.
    /*for (i = 0; i <= hal->hal_count; ++i) {
      ret = ct_process_item_async(hai, hal->hal_flags);
      hai = hai_next(hai);
      }*/
  }
  llapi_hsm_copytool_unregister(&cpt_data);
  return (ret);
}

static int	cpt_setup(t_opt *cpt_opt) {
  int		ret;

  arch_fd = -1;
  if ((arch_fd = open(cpt_opt->ring_mp, O_RDONLY)) < 0) {
    ret = -errno;
    fprintf(stdout, "Can't open archive at '%s'.\n",
	    cpt_opt->ring_mp);
    return (ret);
  }
  ret = llapi_search_fsname(cpt_opt->o_mnt, fs_name);
  if (ret < 0) {
    fprintf(stdout, "Cannot find a Lustre FS mounted at '%s'.\n",
	    cpt_opt->o_mnt);
    return (ret);
  }
  if ((cpt_opt->o_mnt_fd = open(cpt_opt->o_mnt, O_RDONLY)) < 0) {
    ret = -errno;
    fprintf(stdout, "Cannot open mount point at '%s'.\n",
	    cpt_opt->o_mnt);
    return (ret);
  }
  fprintf(stdout, "Debug mode : Setup worked.\n");
  return (ret);
}

/*
** Main.
*/

int		main(int ac, char **av) {
  int		ret;
  t_opt		cpt_opt;
  
  init_opt(&cpt_opt);
  if ((ret = get_opt(ac, av, &cpt_opt)) < 0) {
    return (ret);
  }
  if (cpt_opt.is_daemon) {
    if ((ret = (daemonize(&cpt_opt))) < 0) {
      //free_end(&cpt_opt);
      return (ret);
    }
  }
  fprintf(stdout, "(dev msg) Values retrieved :\n"
	  "is_daemon : %d\n"
	  "is_verbose : %d\n"
	  "ring_mp : %s\n"
	  "lustre_mp : %s\n",
	  cpt_opt.is_daemon,
	  cpt_opt.is_verbose,
	  cpt_opt.ring_mp,
	  cpt_opt.o_mnt);

  //debug mode.
  unsigned int	i;
  for (i = 0; i < cpt_opt.arch_ind_count; i += 1)
    fprintf(stdout, "(dev msg) Archive index retrieved :\n"
	    "index %d\n",
	    cpt_opt.arch_ind[i]);

  if ((ret = cpt_setup(&cpt_opt)) < 0)
    return (ret);
  if ((ret = cpt_run(&cpt_opt)) < 0)
    return (ret);
  if ((ret = cpt_cleanup(&cpt_opt)) < 0)
    return (ret);
  return (ret);
}
