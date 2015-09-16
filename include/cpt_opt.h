#ifndef		CPT_OPT_H
# define	CPT_OPT_H

# include	<asm/types.h>

# define	MIN_OPT		(4)
# define	MAX_ARCH	(sizeof(__u32) * 8)

typedef struct	s_opt
{
  int		is_daemon;
  int		is_verbose;
  int		arch_ind[MAX_ARCH];
  unsigned int	arch_ind_count;
  char		*ring_mp;
  char		*lustre_mp;
}		t_opt;

#endif		/* CPT_OPT_H_ */
