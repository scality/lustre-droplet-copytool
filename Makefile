CC              := gcc

CFLAGS		+= -O2 -Wno-unused-parameter -Wno-sign-compare

DFLAGS		:= -g3

LDFLAGS		+= -lpthread -lbsd -L/usr/local/lib/ -ldroplet -L/lib64/ -lcrypto -llustreapi

RM              := rm -rf

NAME            := lhsmtool_scality

DEBUG		:= lhsmtool_scality_debug

LIB_NAME        := my

HEADER_PATH     := include/

LUSTRE_STABLE	:= lustre-stable/

LIB_PATH        := lib/

SRCS_P		:= srcs/

MSGS_P		:= $(SRCS_P)msgs/

OBJS_P		:= $(SRCS_P)objs/

SRCS		:= $(SRCS_P)lhsmtool_scality.c\
		   $(MSGS_P)trace.c\
		   $(MSGS_P)usage.c

TMP		:= lhsmtool_scality.o\
		   trace.o\
		   usage.o

OBJS            := $(OBJS_P)lhsmtool_scality.o\
		   $(OBJS_P)trace.o\
		   $(OBJS_P)usage.o

all: 		$(NAME)

$(NAME):
		@if [ -d "./srcs/objs" ]; then \
		echo "objs exists, proceeding with make"; else \
		mkdir "./srcs/objs"; \
		fi
		$(CC) -c $(SRCS) $(CFLAGS) -I$(HEADER_PATH) -I$(LUSTRE_STABLE)lustre/include -I$(LUSTRE_STABLE)lnet/include -I$(LUSTRE_STABLE)lustre/utils -I$(LUSTRE_STABLE)libcfs/include -I/usr/local/include/droplet-3.0 -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(NAME) $(OBJS) $(LDFLAGS)

debug:		$(DEBUG)

$(DEBUG):
		@if [ -d "./srcs/objs" ]; then \
		echo "objs exists, proceeding with make"; else \
		mkdir "./srcs/objs"; \
		fi
		$(CC) -c $(SRCS) $(DFLAGS) $(CFLAGS) -I$(HEADER_PATH) -I$(LUSTRE_STABLE)lustre/include -I$(LUSTRE_STABLE)lnet/include -I$(LUSTRE_STABLE)lustre/utils -I$(LUSTRE_STABLE)libcfs/include -I/usr/local/include/droplet-3.0 -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(DEBUG) $(OBJS) $(LDFLAGS)

clean:
		@if [ -d "./srcs/objs" ]; then \
		rm -rf "./srcs/objs"; else \
		echo "objs not found, proceeding"; \
		fi
		@echo "Cleaning objects .o";
		@$(RM) $(OBJS) $(LUSTRE_STABLE)
		@find . -name '*~' -print -delete -o -name '#*#' -print -delete
		@echo "Cleaning temporary objects ~ and #";

fclean:		clean
		@echo "Cleaning objects binary";
		@$(RM) $(NAME)

debugclean:	clean
		@echo "Cleaning objects debug binary";
		@$(RM) $(DEBUG)

re: 		fclean all

red:		fclean debugclean debug

.PHONY: 	all clean flcean re
