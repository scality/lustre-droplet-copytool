#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include "cpt_opt.h"
#include "cpt_msgs.h"
#include <lustre/lustreapi.h>
#include <lustre/lustre_idl.h>
#include <openssl/bn.h>
#include <droplet.h>

/*
** Defines
*/

//#define NDEBUG

#ifdef NDEBUG
#define DPRINTF(fmt, ...)
#else
#define DPRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

#define REPORT_INTERVAL_DEFAULT	30
#define ONE_MB			0x100000
#define	KEY_SIZE	        159
#define	PATH_MAX	        4096

#define	REP_RET_VAL	        1
#define	DPL_DICT_NB		1

static inline double		ct_now(void) {
  struct timeval		tv;
  gettimeofday(&tv, NULL);
  return(tv.tv_sec + 0.000001 * tv.tv_usec);
}

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

static struct hsm_copytool_private	*cpt_data;
static char				fs_name[MAX_OBD_NAME + 1];
static int				arch_fd;
static char				cmd_name[PATH_MAX];

struct		s_opt
{
  int		is_daemon;
  int		is_verbose;
  int		arch_ind[MAX_ARCH];
  unsigned int	arch_ind_count;
  char		*lustre_mp;
  int		lustre_mp_fd;
}		cpt_opt;

#define CT_ERROR(_rc, _format, ...)					\
	llapi_error(LLAPI_MSG_ERROR, _rc,				\
		    "%f %s[%ld]: "_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_DEBUG(_format, ...)						\
	llapi_error(LLAPI_MSG_DEBUG | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_WARN(_format, ...) \
	llapi_error(LLAPI_MSG_WARN | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_TRACE(_format, ...)						\
	llapi_error(LLAPI_MSG_INFO | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

/*
** Initializing and getting pre-running options for the daemon copytool.
*/

void		init_opt(void) {
  cpt_opt.is_daemon = 0;
  cpt_opt.is_verbose = 0;
  cpt_opt.arch_ind_count = 0;
  cpt_opt.lustre_mp = NULL;
  cpt_opt.lustre_mp_fd = -1;
  cpt_data = NULL;
}

/*
** Snprintf func
*/

static int
ct_begin_restore(struct hsm_copyaction_private **phcp,
		 const struct hsm_action_item *hai,
		 int mdt_index,
		 int open_flags) {
  int	 ret;
  char	 src[PATH_MAX];
  
  if ((ret = llapi_hsm_action_begin(phcp, cpt_data, hai, mdt_index, open_flags,
				    false)) < 0) {
    snprintf(src, sizeof(src), "%s/%s/fid/"DFID_NOBRACE, cpt_opt.lustre_mp, dot_lustre_name, PFID(&hai->hai_fid));
    CT_ERROR(ret, "llapi_hsm_action_begin() on '%s' failed\n", src);
  }  
  return (ret);
}

static int
ct_path_archive(char *buf,
		int sz,
		const char *archive_dir,
		const lustre_fid *fid) {
  return (snprintf(buf, sz, "%s/%04x/%04x/%04x/%04x/%04x/%04x/"
		  DFID_NOBRACE, archive_dir,
		  (fid)->f_oid       & 0xFFFF,
		  (fid)->f_oid >> 16 & 0xFFFF,
		  (unsigned int)((fid)->f_seq       & 0xFFFF),
		  (unsigned int)((fid)->f_seq >> 16 & 0xFFFF),
		  (unsigned int)((fid)->f_seq >> 32 & 0xFFFF),
		  (unsigned int)((fid)->f_seq >> 48 & 0xFFFF),
		   PFID(fid)));
}

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
    {"help",		no_argument,		0,			'h'},
    {"verbose",		no_argument,		0,			'v'},
    {"droplet-path",	required_argument,	0,			'p'},
    {"droplet-name",	required_argument,	0,			'n'},
    {0, 0, 0, 0}
  };
  int		opt_ind = 0;
  char		*d_path = NULL;
  char		*d_name = NULL;
  int		ret;

  optind = 0;
  while ((trig = getopt_long(ac, av, "A:r:hvn:p:", long_opts, &opt_ind)) != -1)
    switch (trig) {
    case 'A':
      if ((cpt_opt.arch_ind_count >= MAX_ARCH)
	  || ((unsigned int)atoi(optarg) >= MAX_ARCH)) {
	ret = -E2BIG;
	CT_ERROR(ret, "Archive number must be less"
		"than %zu.", MAX_ARCH);
	return (ret);
      }
      cpt_opt.arch_ind[cpt_opt.arch_ind_count++] = atoi(optarg);
      if (cpt_opt.arch_ind_count <= MAX_ARCH)
	cpt_opt.arch_ind[cpt_opt.arch_ind_count] = 0;
      break;
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
  DPRINTF("name = %s - path = %s\n", d_name, d_path);
  if ((!d_path) || (!d_name)) {
    ret = -EINVAL;
    CT_ERROR(ret, "No specified droplet_profile path or name.");
    return (ret);
  }
  if (ac < 4) {
    ret = -EINVAL;
    CT_ERROR(ret, "Invalid options, try --help or -h for more informations.");
    return (ret);
    }
  if (ac != optind + 1 && optind >= MIN_OPT) {
    ret = -EINVAL;
    CT_ERROR(ret, "No lustre mount point specified.");
    return (ret);
  }
  cpt_opt.lustre_mp = av[optind];
  if (!(*ctx = dpl_ctx_new(d_path, d_name)))
    return (-EINVAL);
  if (cpt_opt.lustre_mp == NULL) {
    ret = -EINVAL;
    CT_ERROR(ret, "Must specify a root directory for the lustre fs.");
    return (ret);
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
    CT_TRACE("Daemon created.");
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
archive_attr(int src_fd,
	       const char *src) {
  char			lov_file[PATH_MAX];
  char			lov_buff[XATTR_SIZE_MAX];
  struct lov_user_md	*lum;
  ssize_t		xattr_size;
  dpl_dict_t		*dict_var;
  dpl_value_t		value_var;
  dpl_sbuf_t		sbuf;
  int			ret;

  dict_var = dpl_dict_new(DPL_DICT_NB);
  DPRINTF("Saving attr.\n");
  if ((xattr_size = fgetxattr(src_fd, XATTR_LUSTRE_LOV, lov_buff,
			      sizeof(lov_buff))) < 0) {
    ret = -EINVAL;
    CT_ERROR(ret, "Cannot get attr info on '%s'", src);
    return (NULL);
  }

  DPRINTF("Archiving %s.\n", lov_buff);
  //
  lum = (void *)lov_buff;

  if (lum->lmm_magic == LOV_USER_MAGIC_V1 ||
      lum->lmm_magic == LOV_USER_MAGIC_V3) {
    lum->lmm_stripe_offset = -1;
  }

  //dpl_sbuf_add(&sbuf, (void *)lum, xattr_size);
  value_var.string = &sbuf;

  sbuf.buf = (void *)lum;
  sbuf.len = xattr_size;
  sbuf.allocated = 0;
  value_var.type = DPL_VALUE_STRING;

  dpl_dict_add_value(dict_var, XATTR_LUSTRE_LOV, &value_var, 0);
  return (dict_var);
}

char *
restore_attr (dpl_dict_t *dict_var) {
  char			*buff;
  struct lov_user_md	*lum;
  int			ret;

  buff = dpl_dict_get_value(dict_var, XATTR_LUSTRE_LOV);
  //FIXME memdup
  return (buff);
}


static int
remove_data(const struct hsm_action_item *hai,
	    const long hal_flags,
	    dpl_ctx_t *ctx,
	    const BIGNUM *BN) {
  char				*BNHEX = NULL;
  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  dpl_status_t			dpl_ret;
  int				ret, ret2;
  
  if (!(BNHEX = BN_bn2hex(BN))) {
    ret = -ENOMEM;
    goto end;
  }

  dpl_ret = dpl_delete_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL);
  if (dpl_ret != DPL_SUCCESS) {
    ret = -EINVAL;
    CT_ERROR(ret, "DPL_DELETE_ID failed for operation remove data = %s.", dpl_status_str(dpl_ret));
    goto end;
  }
  
  CT_TRACE("Dpl_delete_id done successfully for operation remove data.");

  //bug ici, le ret est toujours négatif... pourquoi?
  ret2 = llapi_hsm_action_end(&hcp, &hai->hai_extent, 0, 0 /* ret?? */);
  if (ret2 < 0) {
    CT_ERROR(ret, "Couldn't notify properly the coordinator to end operation remove data.");
    ret = ret2;
    goto end;
  }
  
  CT_TRACE("Coordinator notified properly.");

  ret = 0;
  
 end:
  
  if (BNHEX)
    OPENSSL_free(BNHEX);
  
  return ret;
}

static int
restore_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *BN) {
  char				*buff_data = NULL;
  char				*BNHEX = NULL;
  char				lpath[PATH_MAX];
  __u64				offset;
  int				lustre_fd = -1;
  int				mdt_index = -1;
  int				open_flags = 0;
  bool				set_lovea;
  //
  char				lov_buff[XATTR_SIZE_MAX];
  size_t			lov_size = sizeof(lov_buff);
  //
  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  unsigned int			lenp;
  char				*buff_attr = NULL;
  dpl_dict_t			*dict_var = NULL;
  dpl_status_t			dpl_ret;
  int				ret, ret2;

  if ((lustre_fd = llapi_hsm_action_get_fd(hcp)) < 0) {
    ret = -1;
    CT_ERROR(ret, "Cannot open Lustre fd for operation restore data.");
    goto end;
  }

  if ((ret2 = llapi_get_mdt_index_by_fid(cpt_opt.lustre_mp_fd, &hai->hai_fid, &mdt_index)) < 0) {
    CT_ERROR(ret, "Cannot get mdt index for operation restore data.");
    ret = ret2;
    goto end;
  }

  if (!(BNHEX = BN_bn2hex(BN))) {
    ret = -ENOMEM;
    goto end;
  }

  //FIXME Range to be implemented for large size data
  if ((dpl_ret = dpl_get_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL, NULL, &buff_data, &lenp, &dict_var, NULL))
      != DPL_SUCCESS) {
    ret = -EINVAL;
    CT_ERROR(ret, "DPL_GET_ID failed for operation restore data = %s.", dpl_status_str(dpl_ret));
    goto end;
  }

  CT_TRACE("Dpl_get_id done successfully for operation restore data.");

  //xattr set
  if (!dict_var) {
    CT_ERROR(ret, "Cannot set lov EA on '%s' for operation restore data.", lustre_fd);
    set_lovea = false;
  } else {
    open_flags |= O_LOV_DELAY_CREATE;
    set_lovea = true;
    buff_attr = restore_attr(dict_var);
  }
  
  if (set_lovea) {
    if ((ret2 = fsetxattr(lustre_fd, XATTR_LUSTRE_LOV, buff_attr, XATTR_SIZE_MAX, XATTR_CREATE)) < 0) {
      CT_ERROR(ret, "Cannot set ATTR properly for action restore.");
      ret = ret2;
      goto end;
    }
  }

  offset = hai->hai_extent.length;

  if ((ret2 = pwrite(lustre_fd, buff_data, lenp, offset)) < 0) {
    CT_ERROR(ret, "Couldn't properly write on Lustre's fd for action restore.");
    ret = ret2;
    goto end;
  }

  if ((ret2 = llapi_hsm_action_end(&hcp, &hai->hai_extent, 0, 0 /* ret ??? */ )) < 0) {
    CT_ERROR(ret2, "Couldn't notify properly the coordinator to end operation restore data.");
    ret = ret2;
    goto end;
  }

  ret = 0;

 end:

  if (buff_data)
    free(buff_data);

  if (BNHEX)
    OPENSSL_free(BNHEX);

  return (ret);
}

static int
archive_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *BN) {
  char				*buff_data = MAP_FAILED;
  int				ret, ret2;
  struct hsm_copyaction_private	*hcp = NULL;
  char				src[PATH_MAX];
  struct stat			src_st;
  int				src_fd = -1;
  char				*BNHEX = NULL;
  char				lstr[PATH_MAX];
  int				hp_flags = 0;
  int				ct_rc = 0;
  dpl_option_t			dpl_opts = {
    .mask = DPL_OPTION_CONSISTENT,
  };
  dpl_status_t			dpl_ret;
  dpl_dict_t			*dict_var;

  ret2 = ct_begin_restore(&hcp, hai, -1, 0);
  if (ret2 < 0) {
    DPRINTF("begin_restore failed\n");
    ret = ret2;
    goto end;
  }

  snprintf(src, sizeof(src), "%s/%s/fid/"DFID_NOBRACE, cpt_opt.lustre_mp, dot_lustre_name, PFID(&hai->hai_dfid));

  src_fd = llapi_hsm_action_get_fd(hcp);
  if (src_fd < 0) {
    DPRINTF("src_fd invalid\n");
    ret = -EINVAL;
    goto end;
  }

  if ((fstat(src_fd, &src_st)) < 0) {
    ret = -errno;
    CT_ERROR(ret, "Couldn't stat '%s' for operation archive data.", src);
    goto end;
  }

  if (!(BNHEX = BN_bn2hex(BN))) {
    ret = -ENOMEM;
    goto end;
  }

  CT_TRACE("BN_bn2hex done successfully for operation archive data.");

  buff_data = mmap(NULL, src_st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
  if (buff_data == MAP_FAILED) {
    ret = errno;
    goto end;
  }

  //FIXME --> segfault
  if (!(dict_var = archive_attr(src_fd, src))) {
    ret = -1;
    goto end;
  }

  dpl_ret = dpl_put_id(ctx, NULL, BNHEX, &dpl_opts, DPL_FTYPE_REG, NULL, NULL, dict_var, NULL, buff_data, src_st.st_size);
  if (dpl_ret != DPL_SUCCESS) {
    ret = -errno;
    CT_ERROR(ret, "DPL_PUT_ID failed for operation archive data = %s.", dpl_status_str(dpl_ret));
    ret = -1;
    goto end;
  } 
  
  CT_TRACE("Dpl_put_id done successfully for operation archive data.");

  CT_TRACE("Action completed, notifying coordinator.");

  //FIXME check ptr value not NULL for hcp

  snprintf(lstr, sizeof(lstr), "%s/%s/fid/"DFID_NOBRACE, cpt_opt.lustre_mp, dot_lustre_name, PFID(&hai->hai_fid));

  if ((ret2 = llapi_hsm_action_end(&hcp, &hai->hai_extent, hp_flags, abs(ct_rc))) < 0) {
    CT_TRACE("Couldn' notify properly the coordinator to end operation archive data.");
    ret = ret2;
    goto end;
  }

  ret = 0;

 end:

  //FIXME munmap

  if (src_fd > 0)
    close(src_fd);

  if (BNHEX)
    OPENSSL_free(BNHEX);

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

  DPRINTF("Action : %d.\n", hai->hai_action);
  sprintf(fid, DFID, PFID(&hai->hai_fid));
  ret = llapi_fid2path(cpt_opt.lustre_mp, fid, path,
		       sizeof(path), &recno, &linkno);
  if (ret < 0)
    CT_ERROR(ret, "Cannot get path of FID '%s' to process action.", fid);
  else
    CT_TRACE("Processing file '%s'.", path);

  switch (hai->hai_action) {
  case HSMA_ARCHIVE:
    CT_TRACE("Commencing archive action.");
    if ((ret = archive_data(hai, hal_flags, ctx, BN)) < 0)
      CT_ERROR(ret, "Archive operation failed.");
    break;
  case HSMA_RESTORE:
    CT_TRACE("Commencing restore action.");
    if ((ret = restore_data(hai, hal_flags, ctx, BN)) < 0)
      CT_ERROR(ret, "Restore operation failed.");
    break;
  case HSMA_REMOVE:
    CT_TRACE("Commencing remove action.");
    if ((ret = remove_data(hai, hal_flags, ctx, BN)) < 0)
      CT_ERROR(ret, "Remove operation failed.");
    break;
  case HSMA_CANCEL:
    return (-REP_RET_VAL);
  default:
    ret = -EINVAL;
    CT_ERROR(ret, "Unknown action %d, on %s.",
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

struct ct_th_data {
  long				hal_flags;
  struct hsm_action_item	*hai;
  dpl_ctx_t			*ctx;
  const BIGNUM			*BN;
};

static void *
cpt_thread(void *data) {
  struct ct_th_data	*cttd = data;
  int			ret;

  ret = process_action(cttd->hai, cttd->hal_flags, cttd->ctx, cttd->BN);

  free(cttd->hai);
  free(cttd);
  pthread_exit((void *)(intptr_t)ret);
}

static int
process_async(const struct hsm_action_item *hai,
		  long hal_flags,
		  dpl_ctx_t *ctx,
		  const BIGNUM *BN) {
  pthread_attr_t	attr;
  pthread_t		thread;
  struct ct_th_data	*data;
  int			ret;


  if (!(data = malloc(sizeof(*data))))
    return (-ENOMEM);

  if (!(data->hai = malloc(hai->hai_len))) {
    free(data);
    return (-ENOMEM);
  }

  memcpy(data->hai, hai, hai->hai_len);
  data->hal_flags = hal_flags;
  data->ctx = ctx;
  data->BN = BN;

  if ((ret = pthread_attr_init(&attr)) != 0) {
    CT_ERROR(ret, "pthread_attr_init failed for '%s' service.", cpt_opt.lustre_mp);
    free(data->hai);
    free(data);
    return (-ret);
  }

  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if ((ret = pthread_create(&thread, &attr, cpt_thread, data)) != 0)
    CT_ERROR(ret, "Cannot create thread for '%s' service.", cpt_opt.lustre_mp);
  pthread_attr_destroy(&attr);
  return (0);
}

static int
cpt_run(dpl_ctx_t *ctx) {
  int		ret;

  ret = llapi_hsm_copytool_register(&cpt_data, cpt_opt.lustre_mp,
				    cpt_opt.arch_ind_count,
				    cpt_opt.arch_ind, 0);
  if (ret < 0) {
    CT_ERROR(ret, "Cannot start copytool interface.");
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
      CT_TRACE("Shutting down.");
      break;
    }
    CT_TRACE("Copytool fs=%s, archive#=%d, item_count=%d.",
	    hal->hal_fsname, hal->hal_archive_id, hal->hal_count);
    //FIXME (?) check fs_name with strcmp.
    hai = hai_first(hal);
    if (hal->hal_count == 0) {
      DPRINTF("Oy oy that was a 0 on the hal->hal_count scale !!\n");
    }
    for (i = 0; i < hal->hal_count; ++i) {
      char			*tmp;

      if (!(BN = uks_key_from_fid(hai->hai_fid.f_seq, hai->hai_fid.f_oid, hai->hai_fid.f_ver)))
	return (-ENOMEM);
      tmp = BN_bn2hex(BN);
      //
      DPRINTF("BIGNUM = %s\n", tmp);
      DPRINTF("Current action : %i\n", hai->hai_action);
      //
      OPENSSL_free(tmp);

      process_async(hai, hal->hal_flags, ctx, BN);

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
  ret = llapi_search_fsname(cpt_opt.lustre_mp, fs_name);
  if (ret < 0) {
    CT_ERROR(ret, "Cannot find a Lustre FS mounted at '%s'.",
	    cpt_opt.lustre_mp);
    return (ret);
  }
  if ((cpt_opt.lustre_mp_fd = open(cpt_opt.lustre_mp, O_RDONLY)) < 0) {
    ret = -errno;
    CT_ERROR(ret, "Cannot open mount point at '%s'.",
	    cpt_opt.lustre_mp);
    return (ret);
  }
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
  DPRINTF("Values retrieved :\n"
	  "is_daemon : %d\n"
	  "is_verbose : %d\n"
	  "lustre_mp : %s\n",
	  cpt_opt.is_daemon,
	  cpt_opt.is_verbose,
	  cpt_opt.lustre_mp);

  //debug mode.
  unsigned int	i;
  for (i = 0; i < cpt_opt.arch_ind_count; i += 1) {
    DPRINTF("Archive index retrieved :\n"
	    "index %d\n",
	    cpt_opt.arch_ind[i]);
  }

  if ((ret = cpt_setup()) < 0)
    return (ret);
  if ((ret = cpt_run(ctx)) < 0)
    return (ret);
  if ((ret = cpt_cleanup()) < 0)
    return (ret);
  return (ret);
}
