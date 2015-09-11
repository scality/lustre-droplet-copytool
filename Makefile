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
		   $(MSGS_P)usage.c

TMP		:= main.o\
		   usage.o

OBJS            := $(OBJS_P)main.o\
		   $(OBJS_P)usage.o

all: 		$(NAME)

$(NAME):
		$(CC) -c $(SRCS) $(CFLAGS) -I$(HEADER_PATH)
		@mv $(TMP) $(OBJS_P)
		$(CC) -o $(NAME) $(OBJS) $(FLAGS)

debug:		$(DEBUG)

$(DEBUG):		
		$(CC) -c $(SRCS) $(DFLAGS) $(CFLAGS) -I$(HEADER_PATH)
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
