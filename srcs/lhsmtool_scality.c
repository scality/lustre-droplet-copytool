 /**
 * @file README.md
 * @author Olivier Conan
 * @date 2015
 * @brief .c containing the lustre-droplet-copytool code.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define	LPF64 "ll"
#define	LPX64 "%#"LPF64"x"

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
#include <lustre/lustre_user.h>
#include <openssl/bn.h>
#include <droplet.h>
#include <droplet/uks/uks.h>

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

#define	DPL_DICT_NB		1

static inline double		ct_now(void)
{
  struct timeval		tv;
  gettimeofday(&tv, NULL);
  return(tv.tv_sec + 0.000001 * tv.tv_usec);
}

struct hsm_copytool_private	*ctdata;

enum ct_action {
    CA_IMPORT = 1,
    CA_REBIND,
    CA_MAXSEQ,
};

struct options
{
	int			 o_copy_attrs;
	int			 o_daemonize;
	int			 o_dry_run;
	int			 o_abort_on_error;
	int			 o_shadow_tree;
	int			 o_verbose;
	int			 o_copy_xattrs;
	int			 o_archive_cnt;
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
		    "%f %s[%ld]: "#_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_DEBUG(_format, ...)						\
	llapi_error(LLAPI_MSG_DEBUG | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "#_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_WARN(_format, ...) \
	llapi_error(LLAPI_MSG_WARN | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "#_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

#define CT_TRACE(_format, ...)						\
	llapi_error(LLAPI_MSG_INFO | LLAPI_MSG_NO_ERRNO, 0,		\
		    "%f %s[%ld]: "#_format,				\
		    ct_now(), cmd_name, syscall(SYS_gettid), ## __VA_ARGS__)

/**
 * @brief Function that initialize cpt_opt structure containing informations such
 * as is the copytool launched as daemon and what parameters it's using
 * (mount point, archive id...).
 *
 * @return No return variable.
 */

void		init_opt(void)
{
  cpt_opt.is_daemon = 0; /**< daemon option */
  cpt_opt.is_verbose = 0; /**< verbose option */
  cpt_opt.arch_ind_count = 0; /**< number of archive index */
  cpt_opt.lustre_mp = NULL; /**< lustre mount point */
  cpt_opt.lustre_mp_fd = -1; /**< lustre mount point fd */
  cpt_data = NULL; /**< hsm_copytool_private structure */
}

/**
 * @brief Function that retrieves options for the copytool. Options that
 * have to be set by default are under that form:
 * # copytool --archive=$NB_ARCHIVE --droplet-path=$DROPLET_PATH
 *   --droplet-name=$DROPLET_NAME /lustre/mount/point
 * Other options are --daemon to run the copytool as daemon, --help to trigger
 * the copytool usage. 
 *
 * @param phcp - hcp copy structure containing the cpt_begin parameters
 * @param hai - hsm_action_item structure
 * @param mdt_index - int variable defining hsm index number
 * @param open_flags - int variable defining flags set
 * @see archive_data()
 * @see restore_data()
 * @see remove_data()
 * @return Returns negative value upon failure or 0 upon success.
 */

int
get_opt(int ac,
	char **av,
	dpl_ctx_t **ctx)
{
  int		trig;
  struct option	long_opts[] = {
    {"archive",		required_argument,	NULL,			'A'}, /**< archive number */
    {"daemon",		no_argument,		&cpt_opt.is_daemon,	1},
    {"help",		no_argument,		0,			'h'},
    {"verbose",		no_argument,		0,			'v'},
    {"droplet-path",	required_argument,	0,			'p'}, /**< droplet profile file's path */
    {"droplet-name",	required_argument,	0,			'n'}, /**< droplet profile file's name */
    {0, 0, 0, 0}
  };
  int		opt_ind = 0;
  char		*d_path = NULL;
  char		*d_name = NULL;
  int		ret;

  optind = 0;
  while ((trig = getopt_long(ac, av, "A:r:hvn:p:", long_opts, &opt_ind)) != -1)
    switch (trig)
      {
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
  if ((!d_path) || (!d_name)) /**< if no droplet name or path are specified */
    {
      ret = -EINVAL;
      CT_ERROR(ret, "No specified droplet_profile path or name.");
      return (ret);
    }
  if (ac < 4)
    {
      ret = -EINVAL;
      CT_ERROR(ret, "Invalid options, try --help or -h for more informations.");
      return (ret);
    }
  if (ac != optind + 1 && optind >= MIN_OPT) /**< if no lustre mount point's path is specified */
    {
      ret = -EINVAL;
      CT_ERROR(ret, "No lustre mount point specified.");
      return (ret);
    }
  cpt_opt.lustre_mp = av[optind]; /**< retrieving lustre mount point's path */
  if (!(*ctx = dpl_ctx_new(d_path, d_name)))
    return (-EINVAL);
  if (cpt_opt.lustre_mp == NULL)
    {
      ret = -EINVAL;
      CT_ERROR(ret, "Must specify a root directory for the lustre fs.");
      return (ret);
    }
  return (1);
}

/**
 * @brief Function that gets the lustre's file path.
 *
 * @return Returns snprintf return value on success or failure.
 */

static int
cpt_get_lustre_path(char *buf,
	       int sz,
	       const char *mnt,
	       const lustre_fid *fid)
{
  return snprintf(buf, sz, "%s/%s/fid/"DFID_NOBRACE, mnt,
		  dot_lustre_name, PFID(fid));
}

/**
 * @brief Function that starts a copytool command by notifying the coordinator
 * the action could complete normally what action is to about to be
 * treated and on wich hsm index it's taking action.
 *
 * @param phcp - hcp copy structure containing the cpt_begin parameters
 * @param hai - hsm_action_item structure
 * @param mdt_index - int variable defining hsm index number
 * @param open_flags - int variable defining flags set
 * @see archive_data()
 * @see restore_data()
 * @see remove_data()
 * @return Returns negative value upon failure or 0 upon success.
 */

static int
cpt_begin(struct hsm_copyaction_private **phcp,
		 const struct hsm_action_item *hai,
		 int mdt_index,
		 int open_flags)
{
  int	 ret;
  char	 src[PATH_MAX];
  
  ret = llapi_hsm_action_begin(phcp, cpt_data, hai, mdt_index, open_flags, false); /**< llapi function to notify coordinator */
  if (ret < 0)
    {
      snprintf(src, sizeof(src), "%s/%s/fid/"DFID_NOBRACE, cpt_opt.lustre_mp,
	       dot_lustre_name, PFID(&hai->hai_fid));
      CT_ERROR(ret, "llapi_hsm_action_begin() on '%s' failed\n", src);
      return (ret);
    }
  DPRINTF("Action begin_restore worked properly.\n");
  return (ret);
}

/**
 * @brief Function that ends a copytool command by notifying the coordinator
 * that everything went well and that the action could complete normally.
 * It will also display informations on the outgoing of the action.
 *
 * @param phcp - hcp copy structure containing the cpt_begin parameters
 * @param hai - hsm_action_item structure
 * @param hp_flags - int variable defining flags used to read/write
 * @param cpt_ret - return value of current action function
 * @see archive_data()
 * @see restore_data()
 * @see remove_data()
 * @return Returns negative value upon failure or 0 upon success.
 */

static int
cpt_end(struct hsm_copyaction_private **phcp,
	const struct hsm_action_item *hai,
	int hp_flags,
	int cpt_ret)
{
  struct hsm_copyaction_private	*hcp; /**< structure that will be used as backup if phcp is NULL */
  char				 lstr[PATH_MAX];
  int				 ret;
  
  CT_TRACE("Action completed, notifying coordinator "
	   "cookie="LPX64", FID="DFID", hp_flags=%d err=%d",
	   hai->hai_cookie, PFID(&hai->hai_fid),
	   hp_flags, -cpt_ret);
  cpt_get_lustre_path(lstr, sizeof(lstr), cpt_opt.lustre_mp, &hai->hai_fid); /**< get lustre mount point's path */
  if (phcp == NULL || *phcp == NULL) { /**< check if phcp wasn't initiated */
    ret = llapi_hsm_action_begin(&hcp, ctdata, hai, -1, 0, true); /**< if phcp is NULL initiate it */
    if (ret < 0)
      {
	CT_ERROR(ret, "llapi_hsm_action_begin() on '%s' failed",
		 lstr);
	return ret;
      }
    phcp = &hcp;
  }
  ret = llapi_hsm_action_end(phcp, &hai->hai_extent, hp_flags, abs(cpt_ret)); /**< notifying coordinator that action is done */
  if (ret == -ECANCELED)
    CT_ERROR(ret, "completed action on '%s' has been canceled: "
	     "cookie="LPX64", FID="DFID, lstr, hai->hai_cookie,
	     PFID(&hai->hai_fid));
  else if (ret < 0)
    CT_ERROR(ret, "llapi_hsm_action_end() on '%s' failed", lstr);
  else
    CT_TRACE("llapi_hsm_action_end() ok (ret=%d)",
	     ret);
  return ret;
}

/**
 * @brief Trigger daemon mode upon receiving right options.
 *
 * @see main()
 */

int
daemonize()
{
  int		ret;
  
  if (cpt_opt.is_daemon)
    {
      if ((ret = daemon(1, 1)) < 0)
	return (-(ret = errno));
      CT_TRACE("Daemon created.");
    }
  return (1);
}

/**
 * @brief Signal handler for SIGINT, SIGTERM, will unregister from
 * the coordinator before exiting the copytool.
 *
 * @see cpt_run()
 */

static void
sighand(int sig)
{
  psignal(sig, "exiting");
  llapi_hsm_copytool_unregister(&cpt_data); /**< Upon receiving signal unregister the copytool */
  exit(1);
}

/**
 * @brief Function that will use dpl_dict_add_value in order to 
 * store the xattr in a dpl_dict_t format to be used as parameter
 * for the dpl_put_id.
 *
 * @param src_fd - wich is the fd of the xattr's file
 * @param src - wich is the path of the xattr's file
 * @see restore_attr()
 * @see archive_data()
 * @return Returns a dpl_dict_t * containing the xattr upon success or
 * NULL upon failure.
 */

dpl_dict_t *
archive_attr(int src_fd,
	       const char *src)
{
  char			lov_file[PATH_MAX];
  char			lov_buff[XATTR_SIZE_MAX];
  struct lov_user_md	*lum;
  ssize_t		xattr_size;
  dpl_dict_t		*dict_var;
  dpl_value_t		value_var;
  dpl_sbuf_t		sbuf;
  int			ret;

  dict_var = dpl_dict_new(DPL_DICT_NB);
  DPRINTF("Commencing action saving xattr.\n");
  xattr_size = fgetxattr(src_fd, XATTR_LUSTRE_LOV, lov_buff,
			 sizeof(lov_buff));
  if (xattr_size < 0)
    {
      ret = -EINVAL;
      CT_ERROR(ret, "Cannot get xattr info on '%s'", src);
      return (NULL);
  }

  DPRINTF("Archiving xattr %s.\n", lov_buff);

  lum = (void *)lov_buff;

  if (lum->lmm_magic == LOV_USER_MAGIC_V1 ||
      lum->lmm_magic == LOV_USER_MAGIC_V3)
    lum->lmm_stripe_offset = -1;

  value_var.string = &sbuf;

  sbuf.buf = (void *)lum;
  sbuf.len = xattr_size;
  sbuf.allocated = 0;
  value_var.type = DPL_VALUE_STRING;

  dpl_dict_add_value(dict_var, XATTR_LUSTRE_LOV, &value_var, 0); /**< adding attr structure to dpl_dict_t */
  return (dict_var);
}

/**
 * @brief Function that will use dpl_dict_get_value in order to 
 * bufferize xattr that were saved on the Ring in the format
 * dpl_dict_t.
 *
 * @param dict_var - dpl_dict_t containing the xattr
 * @see archive_attr()
 * @see restore_data()
 * @return Returns a char * containing the xattr upon success or
 * @return NULL upon failure.
 */

int
restore_attr (dpl_dict_t *dict_var, int lustre_fd)
{
  dpl_dict_var_t	*dict_cl = NULL;
  dpl_value_t		*val = NULL;
  dpl_sbuf_t		*sbuf = NULL;
  char			*buff = NULL;
  ssize_t		xattr_size = -1;
  int			ret = 0;

  dict_cl = dpl_dict_get(dict_var, XATTR_LUSTRE_LOV);
  if (dict_cl == NULL)
    {
      DPRINTF("dpl_dict_get failed to retrieve xattr.\n");
      return (-1);
    }
  val = dict_cl->val;
  sbuf = val->string;
  buff = sbuf->buf;
  xattr_size = sbuf->len;

  if (buff == NULL || xattr_size < 0)
    {
      CT_ERROR(-EINVAL, "Couldn't restore xattr from dpl_dict_get_value.");
      ret = -EINVAL;
      return (ret);
    }

  ret = fsetxattr(lustre_fd, XATTR_LUSTRE_LOV, buff, xattr_size, XATTR_CREATE); /**< setting attr to the file's fd */
  if (ret < 0)
    {
      CT_ERROR(ret, "Fsetxattr failed.");
      return (ret);
    }
  else
    CT_TRACE("Fsetxattr succeded for action restore attr.");

  return (ret);
}

/**
 * @brief Function will read from a file descriptor, bufferize the data
 * and use dpl_put_id to store it on the ring using a UKS key generated
 * from the Lustre FID.
 *
 * @param hai - hsm_action_item structure
 * @param hal_flags - hsm_action_list flags
 * @param ctx - dpl_ctx_t droplet structure
 * @param BN - BIGNUM used as UKS key to store on the Ring
 * @see uks_key_from_fid()
 * @see restore_data()
 * @see remove_data()
 * @return Returns a negative value upon failure or 0 upon success.
 */

static int
archive_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *bn)
{
  char				*buff_data = NULL;
  size_t			buff_len = 0;
  int				ret, ret2;
  struct hsm_copyaction_private	*hcp = NULL;
  char				src[PATH_MAX];
  struct stat			src_st;
  int				src_fd = -1;
  int				rcf = 0;
  char				bnhex[DPL_UKS_BCH_LEN + 1];
  int				ct_rc = 0;
  dpl_option_t			dpl_opts = {
    .mask = 0,
  };
  dpl_status_t			dpl_ret;
  dpl_dict_t			*dict_var;

  ret2 = cpt_begin(&hcp, hai, -1, 0); /**< notifying coordinator of start */
  if (ret2 < 0)
    goto end;

  cpt_get_lustre_path(src, sizeof(src), cpt_opt.lustre_mp, &hai->hai_dfid);

  CT_TRACE("Archiving '%s' to the ring.", src);

  src_fd = llapi_hsm_action_get_fd(hcp); /**< llapi function that gets the file's fd */
  if (src_fd < 0)
    {
      ret = src_fd;
      CT_ERROR(ret, "Cannot open '%s' for read.", src);
      goto end;
    }

  if ((fstat(src_fd, &src_st)) < 0)
    {
      ret = -errno;
      CT_ERROR(ret, "Couldn't stat '%s' for operation archive data.", src);
      goto end;
    }
  if (!S_ISREG(src_st.st_mode)) /**< if not a regular file, do not treat */
    {
      ret = -EINVAL;
      CT_ERROR(ret, "'%s' is not a regular file.", src);
      return (ret);
    }

  buff_len = src_st.st_size;

  ret = dpl_uks_bn2hex(bn, bnhex);
  if (ret != DPL_SUCCESS)
    {
      ret = -ENOMEM;
      goto end;
    }

  DPRINTF("BN_bn2hex done successfully for operation archive data.\n");

  if (src_st.st_size > 0)
    {
      buff_data = mmap(NULL, src_st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0); /**< mmaping the data */
      if (buff_data == MAP_FAILED)
	{
	  ret = errno;
	  CT_ERROR(ret, "Either mmap failed with '%s'.\n", strerror(errno));
	  goto end;
	}
    }

  if (src_st.st_size <= 0) /**< If there is no data, the file will still be archived with only the xattrs */
    {
      CT_TRACE("The specified file has no data. Only xattrs will be archived on the Ring.\n"
	       "If your file was supposed to contain data, please remove from the Ring and "
	       "ensure yourself that the file is not corrupted in any ways.\n");
    }

  if (!(dict_var = archive_attr(src_fd, src))) /**< retrieving the attr */
    {
      ret = -1;
      goto end;
    }

  dpl_ret = dpl_put_id(ctx, NULL, bnhex, &dpl_opts, DPL_FTYPE_REG, NULL,
		       NULL, dict_var, NULL, buff_data, src_st.st_size); /**< archiving the data and the attr on the Ring*/
  if (dpl_ret != DPL_SUCCESS)
    {
      ret = -errno;
      CT_ERROR(ret, "DPL_PUT_ID failed for operation archive data = '%s'.", dpl_status_str(dpl_ret));
      ret = -1;
      goto end;
    }
  
  CT_TRACE("Dpl_put_id done successfully for operation archive data.");

  ret = 0;

 end:

  if (buff_data != MAP_FAILED && buff_data != NULL)
    {
      ret2 = munmap(buff_data, buff_len);
      if (ret2 < 0)
	CT_ERROR(ret2, "Munmap failed on operation archive data.");
    }

  ret2 = cpt_end(&hcp, hai, 0, ret); /**< notify coordinator end */
  DPRINTF("Coordinator successfully notified for action archive.\n");
  ret = ret2;

  if (!(src_fd < 0))
    {
      DPRINTF("Fd for archive : Successfully closed.\n");
      close(src_fd);
    }

  DPRINTF("Archive operation successfully ended.\n");
  return (ret);
}

/**
 * @brief Function will use dpl_get_id with the same generated UKS key
 * that the one used to archive it, to retrieve bufferized data
 * and write it on the Lustre's data adress using its fd.
 *
 * @param hai - hsm_action_item structure
 * @param hal_flags - hsm_action_list flags
 * @param ctx - dpl_ctx_t droplet structure
 * @param BN - BIGNUM used as UKS key to restore on the Ring
 * @see uks_key_from_fid()
 * @see archive_data()
 * @see remove_data()
 * @return Returns a negative value upon failure or 0 upon success.
 */

static int
restore_data(const struct hsm_action_item *hai,
	     const long hal_flags,
	     dpl_ctx_t *ctx,
	     const BIGNUM *bn)
{
  char				*buff_data = NULL;
  char				bnhex[DPL_UKS_BCH_LEN + 1];
  char				lpath[PATH_MAX];
  __u64				offset;
  int				lustre_fd = -1;
  int				mdt_index = -1;
  int				open_flags = 0;
  bool				set_lovea;

  char				lov_buff[XATTR_SIZE_MAX];
  size_t			lov_size = sizeof(lov_buff);

  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = 0,
  };
  unsigned int			lenp;
  char				*buff_attr = NULL;
  dpl_dict_t			*dict_var = NULL;
  dpl_status_t			dpl_ret;
  int				ret, ret2;
  struct lu_fid			dfid;

  ret = dpl_uks_bn2hex(bn, bnhex);
  if (ret != DPL_SUCCESS)
    {
      ret = -ENOMEM;
      goto end;
    }

  DPRINTF("Using BNHEX = %s.\n", bnhex);

  dpl_ret = dpl_get_id(ctx, NULL, bnhex, &dpl_opts, DPL_FTYPE_REG, NULL, NULL, &buff_data, &lenp, &dict_var, NULL); /**< retrieve the data and attr from the Ring*/
  if (dpl_ret != DPL_SUCCESS)
    {
      ret = -EINVAL;
      CT_ERROR(ret, "Dpl_get_id failed for operation restore data = '%s'.", dpl_status_str(dpl_ret));
      goto end;
    }

  CT_TRACE("Dpl_get_id done successfully for operation restore data, retrieved : %s", buff_data);

  if (!dict_var)
    {
      CT_ERROR(ret, "Cannot set lov xattr on '%s' for operation restore data, using defaults.", lustre_fd);
      set_lovea = false;
    }
  else
    {
      set_lovea = true;
    }

  if ((ret2 = llapi_file_fget_mdtidx(cpt_opt.lustre_mp_fd, &mdt_index)) < 0) /**< llapi function to get index */
    {
      CT_ERROR(ret, "Cannot get mdt index for operation restore data.");
      ret = ret2;
      goto end;
    }
  DPRINTF("Mdt index retrieved for operation restore data.\n");

  ret2 = cpt_begin(&hcp, hai, mdt_index, open_flags); /**< notifying coordinator of start */
  if (ret2 < 0)
    goto end;
  DPRINTF("Ct begin successfull.\n");

  ret2 = llapi_hsm_action_get_dfid(hcp, &dfid);
  if (ret2 < 0)
    {
      CT_ERROR(ret2, "restoring "DFID" , cannot get FID of created volatile file", PFID(&hai->hai_fid));
      goto end;
    }

  snprintf(lpath, sizeof(lpath), "{VOLATILE}="DFID, PFID(&dfid));
  CT_TRACE("Restoring file from ring to '%s'", lpath);

  if ((lustre_fd = llapi_hsm_action_get_fd(hcp)) < 0) /**< llapi function to get file's fd */
    {
      ret = -1;
      CT_ERROR(ret, "Cannot open Lustre fd for operation restore data.");
      goto end;
    }
  DPRINTF("File fd '%d' successfully opened for operation restore data.\n", lustre_fd);

  //FIXME Range to be implemented for large size data with offset(?).

  if (set_lovea)
    {
      ret2 = restore_attr(dict_var, lustre_fd); /**< get the attr from the dpl_dict_t */ //FIXME check
      if (ret2 < 0)
	{
	  CT_ERROR(ret2, "Cannot set ATTR properly for action restore.");
	  DPRINTF("Error : %s.\n", strerror(errno));
	  ret = ret2;
	}
      else
	DPRINTF("ATTR were set properly for action restore.\n");
    }

  //offset = hai->hai_extent.length; -> Range implementation to be fixed.

  ret2 = write(lustre_fd, buff_data, lenp); /**< writting data on the Lustre file's fd */
  if (ret2 < 0)
    {
      CT_ERROR(ret, "Couldn't properly write on Lustre's fd for action restore.");
      DPRINTF("Error : %s.\n", strerror(errno));
      ret = ret2;
      goto end;
    }
  
  ret = 0;

 end:

  ret2 = cpt_end(&hcp, hai, 0, ret); /**< notifying coordinator of end */
  DPRINTF("Coordinator successfully notified for action restore.\n");
  ret = ret2;

  if (buff_data)
    free(buff_data);

  return (ret);
}

/**
 * @brief Function use dpl_delete_id with the same generated UKS key
 * that was used to archive it in order to delete the data on the
 * Ring.
 *
 * @param hai - hsm_action_item structure
 * @param hal_flags - hsm_action_list flags
 * @param ctx - dpl_ctx_t droplet structure
 * @param BN - BIGNUM used as UKS key to remove from the Ring
 * @see uks_key_from_fid()
 * @see archive_data()
 * @see restore_data()
 * @return Returns a negative value upon failure or 0 upon success.
 */

static int
remove_data(const struct hsm_action_item *hai,
	    const long hal_flags,
	    dpl_ctx_t *ctx,
	    const BIGNUM *bn) {
  char				bnhex[DPL_UKS_BCH_LEN + 1];
  struct hsm_copyaction_private	*hcp = NULL;
  dpl_option_t			dpl_opts = {
    .mask = 0,
  };
  dpl_status_t			dpl_ret;
  int				ret, ret2;

  ret2 = cpt_begin(&hcp, hai, -1, 0); /**< notifying coordinator of start */
  if (ret2 < 0)
    goto end;

  ret = dpl_uks_bn2hex(bn, bnhex);
  if (ret != DPL_SUCCESS)
    {
      ret = -ENOMEM;
      goto end;
    }

  dpl_ret = dpl_delete_id(ctx, NULL, bnhex, &dpl_opts, DPL_FTYPE_REG, NULL); /**< removing data from the Ring */
  if (dpl_ret != DPL_SUCCESS)
    {
      ret = -EINVAL;
      CT_ERROR(ret, "Dpl_delete_id failed for operation remove data = '%s.", dpl_status_str(dpl_ret));
      goto end;
    }
  else
    {
      ret = 0;
    }
  
  CT_TRACE("Dpl_delete_id done successfully for operation remove data.");

  ret = 0;
  
  goto end;
  
 end:

  ret2 = cpt_end(&hcp, hai, 0, ret); /**< notifying coordinator of end */
  CT_TRACE("Coordinator notified properly.");
  ret = ret2;
  
  return ret;
}

/**
 * @brief Function that will use hai structure to interpret commands given
 * to the copytool, if it's either archive, restore or remove on the hai->hai_action.
 * Hai stands for HSM Action Item, wich is part of a list (hal).
 *
 * @param hai - hsm_action_item structure
 * @param hal_flags - hsm_action_list flags
 * @param ctx - dpl_ctx_t droplet structure
 * @param BN - BIGNUM used as UKS key to archive, restore, remove
 * @see archive_data()
 * @see restore_data()
 * @see remove_data()
 * @return Returns 0 upon success and a negative value upon failure.
 */

static int
process_action(const struct hsm_action_item *hai,
	       const long hal_flags,
	       dpl_ctx_t *ctx,
	       const BIGNUM *bn)
{
  int				ret, ret2;
  char				fid[128];
  char				path[PATH_MAX];
  long long			recno = -1;
  int				linkno = 0;

  sprintf(fid, DFID, PFID(&hai->hai_fid));
  ret = llapi_fid2path(cpt_opt.lustre_mp, fid, path,
		       sizeof(path), &recno, &linkno);
  if (ret < 0)
    CT_ERROR(ret, "Cannot get path of FID '%s' to process action.", fid);
  else
    CT_TRACE("Processing file '%s'.", path);

  switch (hai->hai_action)
    {
    case HSMA_ARCHIVE:
      CT_TRACE("Commencing archive action.");
      if ((ret = archive_data(hai, hal_flags, ctx, bn)) < 0)
	CT_ERROR(ret, "Archive operation failed.");
      break;
    case HSMA_RESTORE:
      CT_TRACE("Commencing restore action.");
      if ((ret = restore_data(hai, hal_flags, ctx, bn)) < 0)
	CT_ERROR(ret, "Restore operation failed.");
      break;
    case HSMA_REMOVE:
      CT_TRACE("Commencing remove action.");
      if ((ret = remove_data(hai, hal_flags, ctx, bn)) < 0)
	CT_ERROR(ret, "Remove operation failed.");
      break;
    case HSMA_CANCEL:
      return 0;
      break;
    default:
      ret = -EINVAL;
      CT_ERROR(ret, "Unknown action %d, on %s.",
	       hai->hai_action, cpt_opt.lustre_mp);
      ret2 = cpt_end(NULL, hai, 0, ret);
      ret = ret2;
    }
  return (ret);
}

/**
 * @brief Function that will use the fid provided by Lustre in
 * order to generate a UKS key used to locate the data on the Ring
 * for actions archive, restore and remove.
 * The UKS key will be calculated from the fid using the following
 * pattern :
 * # hash [24 md5(total) ^ 8 f_ver lsb] / oid [f_seq] / volid [f_oid] / srv[NULL] / spec [f_ver 24 msb]
 *
 * @param f_seq - fid sequence
 * @param f_oid - fid oid
 * @param f_ver - fid version
 * @see cpt_run()
 * @return Returns NULL upon failure and a BIGNUM upon success.
 */

BIGNUM *
uks_key_from_fid(int64_t f_seq,
		 int32_t f_oid,
		 int32_t f_ver)
{
  uint32_t		BN_hash;
  int			ver_lsb;
  BIGNUM		*bn;

  if (!(bn = BN_new()))
    return (NULL);
  BN_set_bit(bn, KEY_SIZE);
  BN_clear_bit(bn, KEY_SIZE);
  ver_lsb = f_ver & 0xFF;
  dpl_uks_gen_key(bn, f_seq, f_oid, 0, (f_ver >> 8));
  BN_hash = dpl_uks_hash_get(bn);
  BN_hash ^= ver_lsb;
  dpl_uks_hash_set(bn, BN_hash);
  return (bn);
}

struct ct_th_data
{
  long				hal_flags;
  struct hsm_action_item	*hai;
  dpl_ctx_t			*ctx;
  const BIGNUM			*bn;
};

/**
 * @brief Multi-threading function.
 *
 * @param data - ct_th_data structure containing hai, ctx and BN
 * @see process_async()
 * @see process_action()
 * @return No return variable.
 */

static void *
cpt_thread(void *data)
{
  struct ct_th_data	*cttd = data;
  int			ret;

  ret = process_action(cttd->hai, cttd->hal_flags, cttd->ctx, cttd->bn);

  if (cttd->hai)
    free(cttd->hai);
  free(cttd);
  pthread_exit((void *)(intptr_t)ret);
}

/**
 * @brief Function that will launch each command on a competitive way using
 * multi-threading.
 *
 * @param hai - hsm_action_item structure
 * @param hal_flags - hsm_action_list flags
 * @param ctx - dpl_ctx_t droplet structure
 * @param BN - BIGNUM used as UKS key to archive, restore, remove
 * @see cpt_run()
 * @see cpt_thread()
 * @return Returns 0 upon success and negative error value upon failure.
 */

static int
process_async(const struct hsm_action_item *hai,
		  long hal_flags,
		  dpl_ctx_t *ctx,
		  const BIGNUM *bn)
{
  pthread_attr_t	attr;
  pthread_t		thread;
  struct ct_th_data	*data;
  int			ret;


  if (!(data = malloc(sizeof(*data))))
    return (-ENOMEM);

  if (!(data->hai = malloc(hai->hai_len)))
    {
      free(data);
      return (-ENOMEM);
    }

  memcpy(data->hai, hai, hai->hai_len);
  data->hal_flags = hal_flags;
  data->ctx = ctx;
  data->bn = bn;

  if ((ret = pthread_attr_init(&attr)) != 0)
    {
      CT_ERROR(ret, "pthread_attr_init failed for '%s' service.", cpt_opt.lustre_mp);
      if (data->hai)
	free(data->hai);
      free(data);
      return (-ret);
    }

  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  ret = pthread_create(&thread, &attr, cpt_thread, data);
  if (ret != 0)
    CT_ERROR(ret, "Cannot create thread for '%s' service.", cpt_opt.lustre_mp);
  pthread_attr_destroy(&attr);
  return (0);
}

/**
 * @brief Function that contains the loop that will allow to wait
 * for instructions from the copytool.
 * It will handle signals SIGINT and SIGTERM.
 * Action list is handled by structure hal wich stands for HSM Action List.
 *
 * @param ctx - dpl_ctx_t droplet structure
 * @see uks_key_from_fid()
 * @see process_async()
 * @return Returns negative error value upon failure.
 */

static int
cpt_run(dpl_ctx_t *ctx)
{
  int		ret;

  ret = llapi_hsm_copytool_register(&cpt_data, cpt_opt.lustre_mp,
				    cpt_opt.arch_ind_count,
				    cpt_opt.arch_ind, 0); /**< starting copytool on coordinator*/
  if (ret < 0)
    {
      CT_ERROR(ret, "Cannot start copytool interface.");
      return (ret);
    }
  signal(SIGINT, sighand);
  signal(SIGTERM, sighand);

  while (1)
    {
      struct hsm_action_list	*hal;
      struct hsm_action_item	*hai;
      BIGNUM			*bn = NULL;
      int				msg_size;
      int				i = 0;

      CT_TRACE("Waiting for message from kernel.");

      ret = llapi_hsm_copytool_recv(cpt_data, &hal, &msg_size); /**< retrieving hal */
      if (ret == -ESHUTDOWN)
	{
	  CT_TRACE("Shutting down.");
	  break;
	}
      else if (ret < 0)
	{
	  CT_WARN("Cannot receive action list : '%s'.", strerror(-ret));
	  break;
	}

      CT_TRACE("Copytool fs=%s, archive#=%d, item_count=%d.",
	       hal->hal_fsname, hal->hal_archive_id, hal->hal_count);

      if (strcmp(hal->hal_fsname, fs_name) != 0)
	{
	  ret = -EINVAL;
	  CT_ERROR(ret, "'%s' invalid fs name, expecting : '%s'",
		   hal->hal_fsname, fs_name);
	  break;
	}

      hai = hai_first(hal); /**< setting first action item */

      while (++i <= hal->hal_count) /**< while we still have items to treat*/
	{
	  char			*tmp;

	  if (!(bn = uks_key_from_fid(hai->hai_fid.f_seq, hai->hai_fid.f_oid, hai->hai_fid.f_ver)))
	    return (-ENOMEM);
	  tmp = BN_bn2hex(bn);

	  DPRINTF("BIGNUM = %s\n", tmp);
	  DPRINTF("Current action : %i\n", hai->hai_action);

	  OPENSSL_free(tmp);

	  process_async(hai, hal->hal_flags, ctx, bn); /**< @see cpt_thread() */

	  hai = hai_next(hai); /**< going to next item*/
	}
      //llapi_hsm_action_list_free(&hal);
    }

  llapi_hsm_copytool_unregister(&cpt_data);
  return (ret);
}

/**
 * @brief Function is used to check FS Lustre exists and is
 * correctly mounted. It will also open a file descriptor on the
 * Lustre mount point.
 *
 * @see main()
 * @return Returns 0 upon success or negative error value upon failure.
 */

static int
cpt_setup()
{
  int		ret;

  arch_fd = -1;

  ret = llapi_search_fsname(cpt_opt.lustre_mp, fs_name); /**< getting fs name mounted*/
  if (ret < 0)
    {
      CT_ERROR(ret, "Cannot find a Lustre FS mounted at '%s'.",
	       cpt_opt.lustre_mp);
      return (ret);
    }
  if ((cpt_opt.lustre_mp_fd = open(cpt_opt.lustre_mp, O_RDONLY)) < 0) /**< opening fd on the fs mount point */
    {
      ret = -errno;
      CT_ERROR(ret, "Cannot open mount point at '%s'.",
	       cpt_opt.lustre_mp);
      return (ret);
    }
  return (ret);
}

/**
 * @brief Main function.
 *
 * @see get_opt()
 * @see daemonize()
 * @see cpt_setup()
 * @see cpt_run()
 * @return Returns a negative value upon failure
 */

int
main(int ac,
     char **av)
{
  int		ret;
  dpl_ctx_t	*ctx;
  unsigned int	i;
  
  init_opt();
  if ((ret = get_opt(ac, av, &ctx)) < 0)
    return (ret);
  if (cpt_opt.is_daemon)
    if ((ret = (daemonize())) < 0)
      return (ret);

  DPRINTF("Values retrieved :\n"
	  "is_daemon : %d\n"
	  "is_verbose : %d\n"
	  "lustre_mp : %s\n",
	  cpt_opt.is_daemon,
	  cpt_opt.is_verbose,
	  cpt_opt.lustre_mp);

  for (i = 0; i < cpt_opt.arch_ind_count; i += 1)
    {
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
