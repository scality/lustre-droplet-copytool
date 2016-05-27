#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([lhsmtool_scality], [0.1], [olivier.conan@scality.com])
AM_INIT_AUTOMAKE(lhsmtool_scality, 0.1)
AC_CONFIG_SRCDIR([srcs/lhsmtool_scality.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([bsd], [random], [], [
         echo "bsd library is missing."])
AC_CHECK_LIB([crypto], [crypt], [], [
         echo "crypto library is missing."])
AC_CHECK_LIB([droplet], [dpl_init], [], [
         echo "droplet library is missing."])
AC_CHECK_LIB([lustreapi], [llapi_ping], [], [
         echo "lustreapi library is missing."])
AC_CHECK_LIB([pthread], [pthread_create], [], [
         echo "pthread library is missing."])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([gettimeofday munmap strerror llapi_hsm_action_list_free])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
