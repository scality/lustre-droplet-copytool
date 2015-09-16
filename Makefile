CC              := gcc

CFLAGS		:= -O2 -W -Wall -Wextra

DFLAGS		:= -g3

FLAGS		:= -lpthread

RM              := rm -f

NAME            := copytool

DEBUG		:= copytool

LIB_NAME        := my

HEADER_PATH     := include/

LIB_PATH        := lib/

SRCS_P		:= srcs/

MSGS_P		:= $(SRCS_P)msgs/

OBJS_P		:= $(SRCS_P)objs/

SRCS		:= $(SRCS_P)main.c\
		   $(SRCS_P)liblustreapi_hsm.c\
		   $(MSGS_P)usage.c

TMP		:= main.o\
		   liblustreapi_hsm.o\
		   usage.o

OBJS            := $(OBJS_P)main.o\
		   $(OBJS_P)liblustreapi_hsm.o\
		   $(OBJS_P)usage.o

all: 		$(NAME)

$(NAME):
		$(CC) -c $(SRCS) $(CFLAGS) -I$(HEADER_PATH) -I/home/conan_o/lustre-stable/lustre/include -I/home/conan_o/lustre-stable/lnet/include -I/home/conan_o/client-2.7.0-sources/usr/src/lustre-2.7.0 -I/home/conan_o/client-2.7.0-sources/usr/src/lustre-2.7.0/lustre/include -I/home/conan_o/lustre-stable/lustre/utils -I/home/conan_o/lustre-stable/libcfs/include -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE -lbsd -lpthread
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(NAME) $(OBJS) $(FLAGS)

debug:		$(DEBUG)

$(DEBUG):		
		$(CC) -c $(SRCS) $(DFLAGS) $(CFLAGS) -I$(HEADER_PATH) -I/home/conan_o/lustre-stable/lustre/include -I/home/conan_o/lustre-stable/lnet/include -I/home/conan_o/client-2.7.0-sources/usr/src/lustre-2.7.0 -I/home/conan_o/client-2.7.0-sources/usr/src/lustre-2.7.0/lustre/include -I/home/conan_o/lustre-stable/lustre/utils -I/home/conan_o/lustre-stable/libcfs/include -DLUSTRE_UTILS -DCONFIG_LUSTRE_OBD_MAX_IOCTL_BUFFER=8192 -D_GNU_SOURCE -lbsd -lpthread
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(DEBUG) $(OBJS) $(FLAGS)

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
