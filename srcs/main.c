#include <sys/mman.h>
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
#include <lustre/lustre_idl.h>
#include <openssl/bn.h>
#include <droplet.h>

/*
** To change defines
*/

#define REPORT_INTERVAL_DEFAULT	30
#define ONE_MB			0x100000

#define	DPL_DICT_NB		1

enum ct_action {
	CA_IMPORT = 1,
	CA_REBIND,
	CA_MAXSEQ,
};

struct options {
	int			 o_copy_attrs;
	int			 o_daemonize;
	int			 o_dry_run;
	int			 o_abort_on_error;
	int			 o_shadow_tree;
	int			 o_verbose;
	int			 o_copy_xattrs;
	int			 o_archive_cnt;
	int			 o_archive_id[LL_HSM_MAX_ARCHIVE];
	int			 o_report_int;
	unsigned long long	 o_bandwidth;
	size_t			 o_chunk_size;
	enum ct_action		 o_action;
	char			*o_event_fifo;
	char			*o_mnt;
	int			 o_mnt_fd;
	char			*o_hsm_root;
	char			*o_src;
	char			*o_dst;
};

struct options opt = {
	.o_copy_attrs = 1,
	.o_shadow_tree = 1,
	.o_verbose = LLAPI_MSG_INFO,
	.o_copy_xattrs = 1,
	.o_report_int = REPORT_INTERVAL_DEFAULT,
	.o_chunk_size = ONE_MB,
};

/*
** defines and g_var
*/

#define	KEY_SIZE	159
#define	PATH_MAX	4096

#define	REP_RET_VAL	1

static struct hsm_copytool_private	*cpt_data;
static char				fs_name[MAX_OBD_NAME + 1];
static int				arch_fd;

struct		s_opt
{
  int		is_daemon;
  int		is_verbose;
  int		arch_ind[MAX_ARCH];
  unsigned int	arch_ind_count;
  //char		*ring_mp;
  char		*lustre_mp;
  int		lustre_mp_fd;
}		cpt_opt;

/*
** Initializing and getting pre-running options for the daemon copytool.
*/

void		init_opt(void) {
  cpt_opt.is_daemon = 0;
  cpt_opt.is_verbose = 0;
  cpt_opt.arch_ind_count = 0;
  //cpt_opt.ring_mp = NULL;
  cpt_opt.lustre_mp = NULL;
  cpt_opt.lustre_mp_fd = -1;
  cpt_data = NULL;
}

/*
** tmp syntax
*/


/*
** Opt_get
*/

int
get_opt(int ac,
	char **av,
	dpl_ctx_t **ctx) {
  int		trig;
  struct option	long_opts[] = {
    {"archive",		required_argument,	NULL,			'A'},
    {"daemon",		no_argument,		&cpt_opt.is_daemon,	1},
    //{"ring",		required_argument,	0,			'r'},
    {"help",		no_argument,		0,			'h'},
    {"verbose",		no_argument,		0,			'v'},
    {"droplet-path",	required_argument,	0,			'p'},
    {"droplet-name",	required_argument,	0,			'n'},
    {0, 0, 0, 0}
  };
  int		opt_ind = 0;
  char		*d_path = NULL;
  char		*d_name = NULL;

  optind = 0;
  while ((trig = getopt_long(ac, av, "A:r:hvn:p:", long_opts, &opt_ind)) != -1)
    switch (trig) {
    case 'A':
      if ((cpt_opt.arch_ind_count >= MAX_ARCH)
	  || ((unsigned int)atoi(optarg) >= MAX_ARCH)) {
	fprintf(stdout, "Archive number must be less"
		"than %zu.\n", MAX_ARCH);
	return (-E2BIG);
      }
      cpt_opt.arch_ind[cpt_opt.arch_ind_count++] = atoi(optarg);
      if (cpt_opt.arch_ind_count <= MAX_ARCH)
	cpt_opt.arch_ind[cpt_opt.arch_ind_count] = 0;
      break;
      /*
    case 'r':
      cpt_opt.ring_mp = optarg;
      break;
      */
    case 'h':
      return (usage());
    case 'v':
      cpt_opt.is_verbose = 1;
      break;
    case 'p':
      d_path = optarg;
      break;
    case 'n':
      d_name = optarg;
      break;
    }
  fprintf(stdout, "name = %s - path = %s\n", d_name, d_path);
  if ((!d_path) || (!d_name)) {
    fprintf(stdout, "No specified droplet_profile path or name.\n");
    return (-EINVAL);
  }
  if (ac < 4) {
    fprintf(stdout, "Invalid options, try --help or -h for more informations.\n");
    return (-EINVAL);
    }
  if (ac != optind + 1 && optind >= MIN_OPT) {
    fprintf(stdout, "No lustre mount point specified.\n");
    return (-EINVAL);
  }
  cpt_opt.lustre_mp = av[optind];
  if (!(*ctx = dpl_ctx_new(d_path, d_name)))
    return (-EINVAL);
  /*
  if (cpt_opt.ring_mp == NULL || cpt_opt.lustre_mp == NULL) {
    if (cpt_opt.ring_mp == NULL)
      fprintf(stdout, "Must specify a root directory for the ring.\n");
  */
  if (cpt_opt.lustre_mp == NULL) {
    fprintf(stdout, "Must specify a root directory for the lustre fs.\n");
    return (-EINVAL);
  }
  return (1);
}

/*
** Initializing daemon-mode.
*/

int
daemonize() {
  int		ret;
  
  if (cpt_opt.is_daemon) {
    if ((ret = daemon(1, 1)) < 0)
      return (-(ret = errno));
    fprintf(stdout, "Daemon created.\n");
  }
  return (1);
}

/*
** sig_hander
*/

static void
sighand(int sig) {
  psignal(sig, "exiting");
  llapi_hsm_copytool_unregister(&cpt_data);
  exit(1);
}

/*
** get / put data
*/

dpl_dict_t *
archive_stripe(int src_fd,
	       const char *src) {
  char			lov_file[PATH_MAX];
  char			lov_buff[XATTR_SIZE_MAX];
  struct lov_user_md	*lum;
  ssize_t		xattr_size;
  dpl_dict_t		*dict_var;
  dpl_value_t		value_var;

  dict_var = dpl_dict_new(DPL_DICT_NB);
  fprintf(stdout, "Saving stripe.\n");
  if ((xattr_size = fgetxattr(src_fd, XATTR_LUSTRE_LOV, lov_buff,
			      sizeof(lov_buff))) < 0) {
    fprintf(stdout, "Cannot get stripe info on '%s'\n", src);
    return (NULL);
  }
  lum = (void *)lov_buff;

  if (lum->lmm_magic == LOV_USER_MAGIC_V1 ||
      lum->lmm_magic == LOV_USER_MAGIC_V3) {
    lum->lmm_stripe_offset = -1;
  }

  value_var.string->buf = (void *)lum;
  value_var.string->len = xattr_size;
  value_var.string->allocated = 0;
  value_var.type = DPL_VALUE_STRING;

  dpl_dict_add_value(dict_var, XATTR_LUSTRE_LOV, &value_var, 0);
  return (dict_var);
}

struct lov_user_md *
restore_stripe (dpl_dict_t *dict_var) {
  char			*buff;
  struct lov_user_md	*lum;
  int			ret;

  buff = dpl_dict_get_value(dict_var, XATTR_LUSTRE_LOV);
  lum = (void *)buff;
  //FIXME memdup
  return (lum);
}


static int
remove_data(const struct hsm_action_item *hai,
	    const long hal_flags,
	    dpl_ctx_t *ctx,
	    const BIGNUM *BN) {
  char				*BNHEX;
  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  dpl_status_t			dpl_ret;
  int				ret;

  if (!(BNHEX = BN_bn2hex(BN)))
    return (-ENOMEM);
  if ((dpl_ret = dpl_delete_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL)) != DPL_SUCCESS) {
      printf("Pas success sur le DPL_DELETE_ID = %s.\n", dpl_status_str(dpl_ret));
      OPENSSL_free(BNHEX);
      return (-REP_RET_VAL);
    } else {
      printf("Dpl_delete_id done successfully\n");
    }
    return (1);
    if ((ret = llapi_hsm_action_end(&hcp, &hai->hai_extent, 0, abs(0))) < 0) {
      fprintf(stdout, "Couldn' notify properly the coordinator.\n");
      return (ret);
    }
}

static int
restore_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *BN) {
  char				*buff_data;
  char				*BNHEX;
  __u64				offset;
  int				lustre_fd;
  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  unsigned int			lenp;
  struct lov_user_md		*lum;
  dpl_dict_t			*dict_var;
  dpl_status_t			dpl_ret;
  int				ret;

  if (!(BNHEX = BN_bn2hex(BN)))
    return (-ENOMEM);
  //FIXME Range to be implemented for large size data
  if ((dpl_ret = dpl_get_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL, NULL, &buff_data, &lenp, &dict_var, NULL))
      != DPL_SUCCESS) {
    lum = restore_stripe(dict_var);
    printf("Pas success sur le DPL_GET_ID = %s.\n", dpl_status_str(dpl_ret));
    OPENSSL_free(BNHEX);
    return (-REP_RET_VAL);
  } else {
    printf("Dpl_get_id done successfully\n");
  }
  offset = hai->hai_extent.length;
  if ((pwrite(lustre_fd, buff_data, lenp, offset)) < 0)
    return (-REP_RET_VAL);
  OPENSSL_free(BNHEX);

  if (buff_data)
    free(buff_data);
  if ((ret = llapi_hsm_action_end(&hcp, &hai->hai_extent, 0, abs(0))) < 0) {
    fprintf(stdout, "Couldn' notify properly the coordinator.\n");
    return (ret);
  }
  return (ret);
}

static int
archive_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *BN) {
  char				*buff_data;
  int				ret;
  struct hsm_copyaction_private	*hcp = NULL;
  char				src[PATH_MAX];
  struct stat			src_st;
  int				src_fd = -1;
  char				*BNHEX;
  char				lstr[PATH_MAX];
  int				hp_flags = 0;
  int				ct_rc = 0;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  dpl_status_t			dpl_ret;
  dpl_dict_t			*dict_var;

  if ((ret = ct_begin_restore(&hcp, hai, -1, 0)) < 0)
    return (-REP_RET_VAL);
  ct_path_lustre(src, sizeof(src), cpt_opt.lustre_mp, &hai->hai_dfid);
  fprintf(stdout, "DEBUG SRC: %s\n", src);
  src_fd = llapi_hsm_action_get_fd(hcp);
  fprintf(stdout, "DEBUG SRC FD : %d\n", src_fd);
  if ((fstat(src_fd, &src_st)) < 0) {
    fprintf(stdout, "Couldn't stat '%s'\n", src);
    return (-errno);
  }
  if (!(BNHEX = BN_bn2hex(BN)))
    return (-ENOMEM);
  printf("BN_bn2hex done successfully\n");
  buff_data = mmap(NULL, src_st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
  printf("Mmap done successfully\n");

  if (!(dict_var = archive_stripe(src_fd, src)))
    return (-REP_RET_VAL);

  if ((dpl_ret = dpl_put_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL, NULL, dict_var, NULL, buff_data, src_st.st_size))
      != DPL_SUCCESS) {
    printf("Pas success sur le DPL_PUT_ID = %s.\n", dpl_status_str(dpl_ret));
    OPENSSL_free(BNHEX);
    return (-REP_RET_VAL);
  } else {
    printf("Dpl_put_id done successfully\n");
  }

  OPENSSL_free(BNHEX);
  printf("Openssl_free done successfully\n");

  if (src_fd > 0) {
    fprintf(stdout, "fd on %s successfully closed.\n", src);
    close(src_fd);
  }  
  fprintf(stdout, "Action completed, notifying coordinator.\n");

  //FIXME check ptr value not NULL for hcp

  ct_path_lustre(lstr, sizeof(lstr), cpt_opt.lustre_mp, &hai->hai_fid);
  if ((ret = llapi_hsm_action_end(&hcp, &hai->hai_extent, hp_flags, abs(ct_rc))) < 0) {
    fprintf(stdout, "Couldn' notify properly the coordinator.\n");
    return (ret);
  }

  return (ret);
}

static int
process_action(const struct hsm_action_item *hai,
	       const long hal_flags,
	       dpl_ctx_t *ctx,
	       const BIGNUM *BN) {
  int				ret;
  char				fid[128];
  char				path[PATH_MAX];
  long long			recno = -1;
  int				linkno = 0;

  sprintf(fid, DFID, PFID(&hai->hai_fid));
  ret = llapi_fid2path(cpt_opt.lustre_mp, fid, path,
		       sizeof(path), &recno, &linkno);
  if (ret < 0)
    fprintf(stdout, "Cannot get path of FID %s\n", fid);
  else
    fprintf(stdout, "Processing file '%s'\n", path);

  switch (hai->hai_action) {
  case HSMA_ARCHIVE:
    fprintf(stdout, "Commencing archive action.\n");
    if ((ret = archive_data(hai, hal_flags, ctx, BN)) < 0)
      fprintf(stdout, "Archive failed.\n");
    break;
  case HSMA_RESTORE:
    fprintf(stdout, "Commencing restore action.\n");
    if ((ret = restore_data(hai, hal_flags, ctx, BN)) < 0)
      fprintf(stdout, "Restore failed.\n");
    break;
  case HSMA_REMOVE:
    fprintf(stdout, "Commencing remove action.\n");
    if ((ret = remove_data(hai, hal_flags, ctx, BN)) < 0)
      fprintf(stdout, "Remove failed.\n");
    break;
  case HSMA_CANCEL:
    return (-REP_RET_VAL);
  default:
    ret = -EINVAL;
    fprintf(stdout, "Unknown action %d, on %s\n",
	    hai->hai_action, cpt_opt.lustre_mp);
  }
  return (0);
}

/*
** hash [24 md5(total) ^ 8 f_ver lsb] - oid [f_seq] - volid [f_oid] - srv [] - spec [f_ver 24 msb]
*/

BIGNUM *
uks_key_from_fid(int64_t f_seq, int32_t f_oid, int32_t f_ver) {
  uint32_t		BN_hash;
  int			ver_lsb;
  BIGNUM		*BN;

  if (!(BN = BN_new()))
    return (NULL);
  BN_set_bit(BN, KEY_SIZE);
  BN_clear_bit(BN, KEY_SIZE);
  ver_lsb = f_ver & 0xFF;
  dpl_uks_gen_key(BN, f_seq, f_oid, 0, (f_ver >> 8));
  BN_hash = dpl_uks_hash_get(BN);
  BN_hash ^= ver_lsb;
  dpl_uks_hash_set(BN, BN_hash);
  return (BN);
}

/*
** cpt_functions.
*/

static int
cpt_run(dpl_ctx_t *ctx) {
  int		ret;

  ret = llapi_hsm_copytool_register(&cpt_data, cpt_opt.lustre_mp,
				    cpt_opt.arch_ind_count,
				    cpt_opt.arch_ind, 0);
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
    int				i;

    ret = llapi_hsm_copytool_recv(cpt_data, &hal, &msg_size);
    if (ret == -ESHUTDOWN) {
      fprintf(stdout, "Shutting down.\n");
      break;
    }
    fprintf(stdout, "Copytool fs=%s, archive#=%d, item_count=%d.\n",
	    hal->hal_fsname, hal->hal_archive_id, hal->hal_count);
    //FIXME (?) check fs_name with strcmp.
    hai = hai_first(hal);
    for (i = 0; i < hal->hal_count; ++i) {
      char			*tmp;

      if (!(BN = uks_key_from_fid(hai->hai_fid.f_seq, hai->hai_fid.f_oid, hai->hai_fid.f_ver)))
	return (-ENOMEM);
      tmp = BN_bn2hex(BN);
      fprintf(stdout, "BIGNUM = %s\n", tmp);
      fprintf(stdout, "Current action : %i\n", hai->hai_action);
      OPENSSL_free(tmp);
      process_action(hai, hal->hal_flags, ctx, BN);
      hai = hai_next(hai);
    }
  }
  llapi_hsm_copytool_unregister(&cpt_data);
  return (ret);
}

static int
cpt_setup() {
  int		ret;

  arch_fd = -1;
  /*
  if ((arch_fd = open(cpt_opt.ring_mp, O_RDONLY)) < 0) {
    ret = -errno;
    fprintf(stdout, "Can't open archive at '%s'.\n",
	    cpt_opt.ring_mp);
    return (ret);
  }
  */
  ret = llapi_search_fsname(cpt_opt.lustre_mp, fs_name);
  if (ret < 0) {
    fprintf(stdout, "Cannot find a Lustre FS mounted at '%s'.\n",
	    cpt_opt.lustre_mp);
    return (ret);
  }
  if ((cpt_opt.lustre_mp_fd = open(cpt_opt.lustre_mp, O_RDONLY)) < 0) {
    ret = -errno;
    fprintf(stdout, "Cannot open mount point at '%s'.\n",
	    cpt_opt.lustre_mp);
    return (ret);
  }
  fprintf(stdout, "Debug mode : Setup worked.\n");
  return (ret);
}

/*
** Main.
*/

int
main(int ac,
     char **av) {
  int		ret;
  dpl_ctx_t	*ctx;
  
  init_opt();
  if ((ret = get_opt(ac, av, &ctx)) < 0) {
    return (ret);
  }
  if (cpt_opt.is_daemon) {
    if ((ret = (daemonize())) < 0) {
      return (ret);
    }
  }
  fprintf(stdout, "(dev msg) Values retrieved :\n"
	  "is_daemon : %d\n"
	  "is_verbose : %d\n"
	  "lustre_mp : %s\n",
	  cpt_opt.is_daemon,
	  cpt_opt.is_verbose,
	  cpt_opt.lustre_mp);

  //debug mode.
  unsigned int	i;
  for (i = 0; i < cpt_opt.arch_ind_count; i += 1)
    fprintf(stdout, "(dev msg) Archive index retrieved :\n"
	    "index %d\n",
	    cpt_opt.arch_ind[i]);

  if ((ret = cpt_setup()) < 0)
    return (ret);
  if ((ret = cpt_run(ctx)) < 0)
    return (ret);
  if ((ret = cpt_cleanup()) < 0)
    return (ret);
  return (ret);
}
