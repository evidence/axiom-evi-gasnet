AC_DEFUN(GASNET_FIX_SHELL,[
AC_MSG_CHECKING(for good shell)
if test "$BASH" = '' && test `uname` = HP-UX; then
  AC_MSG_RESULT([no, switching to bash])
  case [$]# in
    0) exec bash - "[$]0"        ;;
    *) exec bash - "[$]0" "[$]@" ;;
  esac
else
  AC_MSG_RESULT(yes)
fi])

AC_DEFUN(GASNET_LIBGCC,[
AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(for libgcc link flags, tc_cv_lib_gcc,
[if test "$GCC" = yes; then
  #LIBGCC="`$CC -v 2>&1 | sed -n 's:^Reading specs from \(.*\)/specs$:-L\1 -lgcc:p'`"
  LIBGCC="-L`$CC -print-libgcc-file-name | xargs dirname` -lgcc"
  if test -z "$LIBGCC"; then
    AC_MSG_ERROR(cannot find libgcc)
  fi
fi
tc_cv_lib_gcc="$LIBGCC"])
LIBGCC="$tc_cv_lib_gcc"
AC_SUBST(LIBGCC)
])

AC_DEFUN(GASNET_ENV_DEFAULT,[
AC_MSG_CHECKING(for $1 in environment)
case "$[$1]" in
  '') [$1]="[$2]"
      AC_MSG_RESULT([no, defaulting to \"[$2]\"])
      ;;
  *)  AC_MSG_RESULT([yes, using \"$[$1]\"]) ;;
esac
])

AC_DEFUN(GASNET_OPTION_HELP,[  --$1 substr([                     ],len($1))$2])

AC_DEFUN(GASNET_IF_ENABLED,[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | no) $4 ;;
  *)  $3 ;;
esac
])

AC_DEFUN(GASNET_IF_DISABLED,[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | yes) $4 ;;
  *)   $3 ;;
esac
])

AC_DEFUN(GASNET_IF_ENABLED_WITH_AUTO,[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,$2))
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  no)  $4 ;;
  yes) $3 ;;
  *)   $5 ;;
esac
])

AC_DEFUN(GASNET_SUBST,[
$1="$2"
AC_SUBST($1)])

AC_DEFUN(GASNET_SUBST_FILE,[
$1="$2"
AC_SUBST_FILE($1)])

AC_DEFUN(GASNET_CHECK_PROGS,[
case "$$1" in
  '') AC_CHECK_PROGS($1,$2)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac])

AC_DEFUN(GASNET_PATH_PROGS,[
case "$$1" in
  '') AC_PATH_PROGS($1,$2)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac])

dnl GASNET_CHECK_LIB(library, function, action-if-found, action-if-not-found, other-flags, other-libraries)
AC_DEFUN(GASNET_CHECK_LIB,[
GASNET_check_lib_old_ldflags="$LDFLAGS"
LDFLAGS="$LD_FLAGS $5"
AC_CHECK_LIB($1, $2, $3, $4, $6)
LDFLAGS="$GASNET_check_lib_old_ldflags"])


AC_DEFUN(GASNET_FIX_EXEC,[[
case "$CONFIG_FILES" in
  *$1*) chmod +x $1 ;;
esac]])


dnl GASNET_TRY_CFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN(GASNET_TRY_CFLAG,[
oldflags="$CFLAGS"
CFLAGS="$CFLAGS $1"
AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILE([], [], $2, $3)
AC_LANG_RESTORE
CFLAGS="$oldflags"])


dnl GASNET_TRY_CXXFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN(GASNET_TRY_CXXFLAG,[
oldflags="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $1"
AC_LANG_SAVE
AC_LANG_CPLUSPLUS
AC_TRY_COMPILE([], [], $2, $3)
AC_LANG_RESTORE
CXXFLAGS="$oldflags"])


AC_DEFUN(GASNET_TRY_CACHE_CHECK,[
AC_CACHE_CHECK($1, tc_cv_$2,
AC_TRY_COMPILE([$3], [$4], tc_cv_$2=yes, tc_cv_$2=no))
if test "$tc_cv_$2" = yes; then
  :
  $5
fi])


AC_DEFUN(GASNET_TRY_CACHE_LINK,[
AC_CACHE_CHECK($1, tc_cv_$2,
AC_TRY_LINK([$3], [$4], tc_cv_$2=yes, tc_cv_$2=no))
if test "$tc_cv_$2" = yes; then
  :
  $5
fi])


AC_DEFUN(GASNET_IFDEF,[
AC_TRY_CPP([
#ifndef $1
# error
#endif], $2, $3)])


AC_DEFUN(GASNET_FAMILY_CACHE_CHECK,[
AC_REQUIRE_CPP
AC_CACHE_CHECK(for $1 compiler family, $3, [
  $3=unknown
  GASNET_IFDEF(__GNUC__, $3=GNU)
  GASNET_IFDEF(__xlC__, $3=XLC)
  GASNET_IFDEF(__KCC, $3=KAI)
  GASNET_IFDEF(__SUNPRO_C, $3=Sun)
  GASNET_IFDEF(_CRAYC, $3=Cray)
  GASNET_IFDEF(__INTEL_COMPILER, $3=Intel)
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(mips, $3=MIPS)
  fi
])
$2_FAMILY=$$3
$2_UNWRAPPED=$$2
case $$3 in
  GNU) $2_WRAPPED=$$2 ;;
  *)   $2_WRAPPED="\$(top_builddir)/cc-wrapper \$($2_FAMILY) \$($2_UNWRAPPED)" ;;
esac
AC_SUBST($2_FAMILY)
AC_SUBST($2_UNWRAPPED)
AC_SUBST($2_WRAPPED)
GASNET_SUBST_FILE(cc_wrapper_mk, cc-wrapper.mk)
])


AC_DEFUN(GASNET_FUNC_ALLOCA,[
  AC_SUBST(ALLOCA)
  patsubst(AC_FUNC_ALLOCA, [p = alloca], [p = (char *) alloca])
])


