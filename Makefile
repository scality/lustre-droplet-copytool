CC              := gcc

CFLAGS		+= -O2 -Wno-unused-parameter -Wno-sign-compare

DFLAGS		:= -g3

LDFLAGS		+= -lpthread -lbsd -L/usr/local/lib/ -ldroplet

RM              := rm -f

NAME            := copytool

DEBUG		:= copytool_d

LIB_NAME        := my

HEADER_PATH     := include/

LUSTRE_STABLE	:= lustre-stable/

CLIENT_270	:= client-2.7.0-sources/

LIB_PATH        := lib/

SRCS_P		:= srcs/

MSGS_P		:= $(SRCS_P)msgs/

OBJS_P		:= $(SRCS_P)objs/

SRCS		:= $(SRCS_P)main.c\
		   $(SRCS_P)liblustreapi.c\
		   $(SRCS_P)liblustreapi_hsm.c\
		   $(SRCS_P)liblustreapi_json.c\
		   $(SRCS_P)liblustreapi_kernelconn.c\
		   $(MSGS_P)usage.c

TMP		:= main.o\
		   liblustreapi.o\
		   liblustreapi_hsm.o\
		   liblustreapi_json.o\
		   liblustreapi_kernelconn.o\
		   usage.o

OBJS            := $(OBJS_P)main.o\
		   $(OBJS_P)liblustreapi.o\
		   $(OBJS_P)liblustreapi_hsm.o\
		   $(OBJS_P)liblustreapi_json.o\
		   $(OBJS_P)liblustreapi_kernelconn.o\
		   $(OBJS_P)usage.o

all: 		$(NAME)

$(NAME):
		$(CC) -c $(SRCS) $(CFLAGS) -I$(HEADER_PATH) -I$(LUSTRE_STABLE)lustre/include -I$(LUSTRE_STABLE)lnet/include -I$(LUSTRE_STABLE)lustre/utils -I$(LUSTRE_STABLE)libcfs/include -I$(CLIENT_270)usr/src/lustre-2.7.0 -I$(CLIENT_270)usr/src/lustre-2.7.0/lustre/include -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(NAME) $(OBJS) $(LDFLAGS)

debug:		$(DEBUG)

$(DEBUG):
		$(CC) -c $(SRCS) $(DFLAGS) $(CFLAGS) -I$(HEADER_PATH) -I$(LUSTRE_STABLE)lustre/include -I$(LUSTRE_STABLE)lnet/include -I$(LUSTRE_STABLE)lustre/utils -I$(LUSTRE_STABLE)libcfs/include -I$(CLIENT_270)usr/src/lustre-2.7.0 -I$(CLIENT_270)usr/src/lustre-2.7.0/lustre/include -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(DEBUG) $(OBJS) $(LDFLAGS)

clean:
		$(RM) $(OBJS)

fclean:		clean
		$(RM) $(NAME)

debugclean:	clean
		$(RM) $(DEBUG)

re: 		fclean all

red:		fclean debugclean debug
		find . -name '*~' -print -delete -o -name '#*#' -print -delete

.PHONY: 	all clean flcean re
