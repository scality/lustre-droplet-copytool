AUTOMAKE_OPTIONS = foreign

AM_CFLAGS = -O2 -Wno-unused-parameter -Wno-sign-compare -Iinclude -I/usr/local/include/droplet-3.0 -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE
AM_LDFLAGS = -lpthread -lbsd -L/usr/local/lib/ -ldroplet -L/lib64/ -lcrypto -llustreapi

bin_PROGRAMS = lhsmtool_scality
lhsmtool_scality_SOURCES = srcs/lhsmtool_scality.c\
			srcs/msgs/trace.c\
			srcs/msgs/usage.c\
			include/cpt_msgs.h\
			include/cpt_opt.h\
			include/lustre_ver.h\
			include/trace.h
