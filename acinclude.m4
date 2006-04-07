dnl   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/acinclude.m4,v $
dnl     $Date: 2006/04/07 17:29:16 $
dnl $Revision: 1.99 $
dnl Description: m4 macros
dnl Copyright 2004,  Dan Bonachea <bonachea@cs.berkeley.edu>
dnl Terms of use are as specified in license.txt

dnl insert comments to improve readability of generated configure script
pushdef([gasnet_fun_level],0)
define([gasnet_fun_comment],[# $1])
AC_DEFUN([GASNET_FUN_BEGIN],[
pushdef([gasnet_fun_level],incr(defn([gasnet_fun_level])))
gasnet_fun_comment(vvvvvvvvvvvvvvvvvvvvvv [$1] vvvvvvvvvvvvvvvvvvvvvv (L:gasnet_fun_level))
])
AC_DEFUN([GASNET_FUN_END],[
gasnet_fun_comment(^^^^^^^^^^^^^^^^^^^^^^ [$1] ^^^^^^^^^^^^^^^^^^^^^^ (L:gasnet_fun_level))
popdef([gasnet_fun_level])
])

dnl determine the autoconf version used to build configure script 
AC_DEFUN([GASNET_GET_AUTOCONF_VERSION],[
GASNET_FUN_BEGIN([$0])
AC_REQUIRE([AC_PROG_AWK])
AC_MSG_CHECKING(autoconf version)
dnl AUTOCONF_VERSION=`cat ${srcdir}/configure | perl -e '{ while (<STDIN>) { if (m/enerated.*utoconf.*([[0-9]]+)\.([[0-9]]+).*/) { print "[$]1.[$]2\n"; exit 0 } } }'`
AUTOCONF_VERSION_STR=`cat ${srcdir}/configure | $AWK '/.*enerated.*utoconf.*([[0-9]]+).([[0-9]]+).*/ { [match]([$]0,"[[0-9]]+.[[0-9]]+"); print [substr]([$]0,RSTART,RLENGTH); exit 0 } '`
AUTOCONF_VERSION=`echo $AUTOCONF_VERSION_STR | $AWK -F. '{ printf("%i%i",[$]1,[$]2); }'`
AC_MSG_RESULT($AUTOCONF_VERSION_STR)
GASNET_FUN_END([$0])
])

dnl GASNET_GCC_VERSION_CHECK(type)  type=CC or CXX
AC_DEFUN([GASNET_GCC_VERSION_CHECK],[
GASNET_FUN_BEGIN([$0($1)])
AC_MSG_CHECKING(for known buggy compilers)
badgccmsg=""
AC_TRY_COMPILE([
#if __GNUC__ == 2 && __GNUC_MINOR__ == 96 && __GNUC_PATCHLEVEL__ == 0
# error
#endif
],[ ], [:], [
AC_MSG_RESULT([$1] is gcc 2.96)
badgccmsg="Use of gcc/g++ 2.96 for compiling this software is strongly discouraged. \
It is not an official GNU release and has many serious known bugs, especially \
in the optimizer, which may lead to bad code and incorrect runtime behavior. \
Consider using \$[$1] to select a different compiler."
GASNET_IF_ENABLED(allow-gcc296, Allow the use of the broken gcc/g++ 2.96 compiler, [
  AC_MSG_WARN([$badgccmsg])
  ],[
  AC_MSG_ERROR([$badgccmsg \
  You may enable use of this broken compiler at your own risk by passing the --enable-allow-gcc296 flag.])
])
])
AC_TRY_COMPILE([
#if __GNUC__ == 3 && __GNUC_MINOR__ == 2 && __GNUC_PATCHLEVEL__ <= 2
# error
#endif
],[ ], [:], [
AC_MSG_RESULT([$1] is gcc 3.2.0-2)
badgccmsg="Use of gcc/g++ 3.2.0-2 for compiling this software is strongly discouraged. \
This version has a serious known bug in the optimizer regarding structure copying, \
which may lead to bad code and incorrect runtime behavior when optimization is enabled. \
Consider using \$[$1] to select a different compiler."
GASNET_IF_ENABLED(allow-gcc32, Allow the use of the known broken gcc/g++ 3.2.0-2 compiler, [
  AC_MSG_WARN([$badgccmsg])
  ],[
  AC_MSG_ERROR([$badgccmsg \
  You may enable use of this broken compiler at your own risk by passing the --enable-allow-gcc32 flag.])
])
])
if test -z "$badgccmsg"; then
  AC_MSG_RESULT(ok)
fi
GASNET_FUN_END([$0($1)])
])

AC_DEFUN([GASNET_FIX_SHELL],[
GASNET_FUN_BEGIN([$0])
AC_MSG_CHECKING(for good shell)
if test "$BASH" = '' && test `uname` = HP-UX; then
  AC_MSG_RESULT([no, switching to bash])
  case [$]# in
    0) exec bash - "[$]0"        ;;
    *) exec bash - "[$]0" "[$]@" ;;
  esac
else
  AC_MSG_RESULT(yes)
fi
GASNET_FUN_END([$0])
])

dnl find full pathname for a given header file, if it exists and AC_SUBST it
AC_DEFUN([GASNET_FIND_HEADER],[
GASNET_FUN_BEGIN([$0($1)])
AC_REQUIRE([AC_PROG_AWK])
AC_CHECK_HEADERS($1)
pushdef([lowername],patsubst(patsubst(patsubst([$1], [/], [_]), [\.], [_]), [-], [_]))
pushdef([uppername],translit(lowername,'a-z','A-Z'))
if test "$ac_cv_header_[]lowername" = "yes"; then
  AC_MSG_CHECKING(for location of $1)
  echo "#include <$1>" > conftest.c
  header_pathname=
  if test "$GASNET_FIND_HEADER_CPP"; then
    echo "$GASNET_FIND_HEADER_CPP conftest.c" >&5
    header_pathname=`$GASNET_FIND_HEADER_CPP conftest.c 2>&5 | grep $1 | head -1`
    header_pathname=`echo $header_pathname | $AWK '{ printf("%s",[$]3); }'`
  fi
  if test -z "$header_pathname"; then
    echo "$CPP conftest.c" >&5
    header_pathname=`$CPP conftest.c 2>&5 | grep $1 | head -1`
    header_pathname=`echo $header_pathname | $AWK '{ printf("%s",[$]3); }'`
  fi
  if test -z "$header_pathname"; then
    # IBM xlc doesn't always emit include file name in output: try /usr/include
    if test -r /usr/include/$1; then
        header_pathname="\"/usr/include/$1\""
    fi
  fi
  if test -z "$header_pathname"; then
    AC_MSG_RESULT(unknown)
    AC_MSG_WARN(Unable to detect pathname of lowername - pretending it doesn't exist)
    have=0
  else
    AC_MSG_RESULT($header_pathname)
    have=1
  fi
else
  header_pathname=
  have=0
fi
PATH_[]uppername=$header_pathname
HAVE_[]uppername=$have
AC_SUBST(PATH_[]uppername)
AC_SUBST(HAVE_[]uppername)
popdef([uppername])
popdef([lowername])
GASNET_FUN_END([$0($1)])
])

dnl do AC_CHECK_SIZEOF and also AC_SUBST the result, second arg is optional prefix
AC_DEFUN([GASNET_CHECK_SIZEOF],[
  GASNET_FUN_BEGIN([$0($1,$2)])
  pushdef([typename],patsubst(patsubst([$1], [\ ], [_]), [\*], [p]))
  pushdef([barename],translit(sizeof_[]typename,'A-Z','a-z'))
  pushdef([lowername],translit($2[]barename,'A-Z','a-z'))
  pushdef([uppername],translit($2[]barename,'a-z','A-Z'))

  if test "$cross_compiling" = "yes" ; then
    GASNET_CROSS_VAR(uppername,uppername)
    ac_cv_[]lowername=$uppername
  fi
  dnl use bare AC_CHECK_SIZEOF here to get correct .h behavior & avoid duplicate defs
  GASNET_PUSHVAR(ac_cv_[]barename,"$ac_cv_[]lowername")
  if test "$ac_cv_[]barename" = "" ; then
    unset ac_cv_[]barename
    unset ac_cv_type_[]typename
  fi
  if test "$2" != "" ; then
    AC_MSG_CHECKING([$2 size:])
  fi
  AC_CHECK_SIZEOF($1, $uppername) 
  gasnet_checksizeoftmp_[]lowername="$ac_cv_[]barename"
  GASNET_POPVAR(ac_cv_[]barename)
  ac_cv_[]lowername=$gasnet_checksizeoftmp_[]lowername
  uppername=$gasnet_checksizeoftmp_[]lowername
  if test "$uppername" = "0" -o "$uppername" = "" -o "$ac_cv_[]lowername" != "$uppername"; then
    AC_MSG_ERROR(failed to find sizeof($1))
  fi
  if test "$2" != ""; then
    dnl work around an irritating autoheader bug - 
    dnl different autoheader versions handle the auto-AC_DEFINE done by
    dnl AC_CHECK_SIZEOF differently. This mantra should ensure we get exactly one
    dnl copy of each def in the config.h.in for any autoheader version
    ac_cv_[]uppername[]_indirect=uppername
    dnl following must appear exactly once to prevent errors
    AC_DEFINE_UNQUOTED($ac_cv_[]uppername[]_indirect,$uppername) 
  fi
  AC_SUBST(uppername)

  popdef([barename])
  popdef([typename])
  popdef([lowername])
  popdef([uppername])
  GASNET_FUN_END([$0($1,$2)])
])

dnl GASNET_CHECK_INTTYPES(headername) 
dnl AC_DEFINE and set HAVE_HEADERNAME_H if the header exists
dnl AC_DEFINE and AC_SUBST COMPLETE_HEADERNAME_H if it contains all the inttypes 
dnl that we care about (all of which are mandated by C99 and POSIX!)
AC_DEFUN([GASNET_CHECK_INTTYPES_HELPERPROG],[
    #include <$1>
    int check() {
    	int8_t    i8;
    	uint8_t   u8;
    	int16_t  i16;
    	uint16_t u16;
    	int32_t  i32;
    	uint32_t u32;
    	int64_t  i64;
    	uint64_t u64;
    	intptr_t  ip;
    	uintptr_t up;
	if (sizeof(i8) != 1) return 1;
	if (sizeof(u8) != 1) return 1;
	if (sizeof(i16) != 2) return 1;
	if (sizeof(u16) != 2) return 1;
	if (sizeof(i32) != 4) return 1;
	if (sizeof(u32) != 4) return 1;
	if (sizeof(i64) != 8) return 1;
	if (sizeof(u64) != 8) return 1;
	if (sizeof(ip) != sizeof(void*)) return 1;
	if (sizeof(up) != sizeof(void*)) return 1;
        return 0;
    }
])
dnl detect whether a given inttypes header is available and complete, with optional prefix
AC_DEFUN([GASNET_CHECK_INTTYPES],[
  GASNET_FUN_BEGIN([$0($1,$2)])
  pushdef([lowername],patsubst(patsubst(patsubst([$1], [/], [_]), [\.], [_]), [-], [_]))
  pushdef([uppername],translit(lowername,'a-z','A-Z'))
 GASNET_PUSHVAR(HAVE_[]uppername,"")
 dnl force a recheck
 unset ac_cv_header_[]lowername 
 AC_CHECK_HEADERS([$1])
 GASNET_POPVAR(HAVE_[]uppername)
 if test "$ac_cv_header_[]lowername" = "yes"; then
  [$2]HAVE_[]uppername=1
  AC_SUBST([$2]HAVE_[]uppername)
  AC_DEFINE([$2]HAVE_[]uppername)
  if test "$cross_compiling" = "yes" ; then
    dnl if cross-compiling, just ensure the header can build the inttypes program and hope for the best
    GASNET_TRY_CACHE_CHECK([for a complete $1],[[$2]COMPLETE_[]uppername],[
      GASNET_CHECK_INTTYPES_HELPERPROG($1)
    ],[ return check(); ], [
      [$2]COMPLETE_[]uppername=1
      AC_SUBST([$2]COMPLETE_[]uppername)
      AC_DEFINE([$2]COMPLETE_[]uppername)
    ])
  else 
    dnl otherwise, build and run the inttypes program to ensure the header values are actually correct
    GASNET_TRY_CACHE_RUN([for a complete $1],[[$2]COMPLETE_[]uppername],[
      GASNET_CHECK_INTTYPES_HELPERPROG($1)
      int main() { return check(); }
    ],[ 
      [$2]COMPLETE_[]uppername=1
      AC_SUBST([$2]COMPLETE_[]uppername)
      AC_DEFINE([$2]COMPLETE_[]uppername)
    ])
  fi
 fi
  popdef([lowername])
  popdef([uppername])
  GASNET_FUN_END([$0($1,$2)])
])

dnl PR828: AM_CONDITIONAL must appear on all control paths
dnl this macro runs them for a prefix which is not encountered
AC_DEFUN([GASNET_SETUP_INTTYPES_DUMMY], [ 
  pushdef([cvsizeof],translit(ac_cv_[$1]sizeof_,'A-Z','a-z'))
  if test x"$[]cvsizeof[]int$[]cvsizeof[]long$[]cvsizeof[]void_p" = x444; then
    $1[]PLATFORM_ILP32=yes
  fi
  if test x"$[]cvsizeof[]int$[]cvsizeof[]long$[]cvsizeof[]void_p" = x488; then
    $1[]PLATFORM_LP64=yes
  fi
  if test x"$[]cvsizeof[]int$[]cvsizeof[]long$[]cvsizeof[]void_p" = x888; then
    $1[]PLATFORM_ILP64=yes
  fi
  dnl following worksaround buggy automake which mishandles m4 expansions in AM_CONDITIONAL
  dnl these versions just shut up its whining (but still provide correct values)
  AM_CONDITIONAL(PLATFORM_ILP32, test "$PLATFORM_ILP32" = "yes") 
  AM_CONDITIONAL(PLATFORM_LP64,  test "$PLATFORM_LP64" = "yes")
  AM_CONDITIONAL(PLATFORM_ILP64, test "$PLATFORM_ILP64" = "yes")
  dnl and now the real versions..
  AM_CONDITIONAL($1[]PLATFORM_ILP32, test "$[$1]PLATFORM_ILP32" = "yes") 
  AM_CONDITIONAL($1[]PLATFORM_LP64,  test "$[$1]PLATFORM_LP64" = "yes")
  AM_CONDITIONAL($1[]PLATFORM_ILP64, test "$[$1]PLATFORM_ILP64" = "yes")
  popdef([cvsizeof])
])

dnl all the inttypes goop required for portable_inttypes.h
dnl second arg is optional prefix for defs
AC_DEFUN([GASNET_SETUP_INTTYPES], [ 
  GASNET_FUN_BEGIN([$0($1,$2)])
  GASNET_CHECK_SIZEOF(char, $1)
  GASNET_CHECK_SIZEOF(short, $1)
  GASNET_CHECK_SIZEOF(int, $1)
  GASNET_CHECK_SIZEOF(long, $1)
  GASNET_CHECK_SIZEOF(long long, $1)
  GASNET_CHECK_SIZEOF(void *, $1)

  GASNET_SETUP_INTTYPES_DUMMY($1) 
 
  GASNET_CHECK_INTTYPES(stdint.h,$1)
  GASNET_CHECK_INTTYPES(inttypes.h,$1)
  GASNET_CHECK_INTTYPES(sys/types.h,$1)
 
  [$1]INTTYPES_DEFINES="-D[$1]SIZEOF_CHAR=$[$1]SIZEOF_CHAR -D[$1]SIZEOF_SHORT=$[$1]SIZEOF_SHORT -D[$1]SIZEOF_INT=$[$1]SIZEOF_INT -D[$1]SIZEOF_LONG=$[$1]SIZEOF_LONG -D[$1]SIZEOF_LONG_LONG=$[$1]SIZEOF_LONG_LONG -D[$1]SIZEOF_VOID_P=$[$1]SIZEOF_VOID_P"
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]HAVE_STDINT_H)
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]COMPLETE_STDINT_H)
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]HAVE_INTTYPES_H)
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]COMPLETE_INTTYPES_H)
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]HAVE_SYS_TYPES_H)
  GASNET_APPEND_DEFINE([$1]INTTYPES_DEFINES, [$1]COMPLETE_SYS_TYPES_H)
 
  AC_SUBST([$1]INTTYPES_DEFINES)
  GASNET_FUN_END([$0($1,$2)])
])


dnl Appends -Dvar_to_define onto target_var, iff var_to_define is set
dnl GASNET_APPEND_DEFINE(target_var, var_to_define)
AC_DEFUN([GASNET_APPEND_DEFINE],[
GASNET_FUN_BEGIN([$0])
  if test "$[$2]" != ""; then
    [$1]="$[$1] -D[$2]"
  fi
GASNET_FUN_END([$0])
]) 

dnl GASNET_SUBST_TEXT(varname, text to subst)
dnl perform subst for multi-line text fields
AC_DEFUN([GASNET_SUBST_TEXT],[
  GASNET_FUN_BEGIN([$0($1,...)])
  mkdir -p "$TOP_BUILDDIR/.subst_text"
  $1="$TOP_BUILDDIR/.subst_text/$1"
  cat > $$1 <<EOF
$2
EOF
  AC_SUBST_FILE($1)
  GASNET_FUN_END([$0($1,...)])
])

dnl push a new value into variable varname, saving the old value
dnl GASNET_PUSHVAR(varname, new value)
AC_DEFUN([GASNET_PUSHVAR],[
  GASNET_FUN_BEGIN([$0($1,$2)])
  dnl echo "old value of $1: $[$1]"
  if test "$_pushcnt_$1" = "" ; then
    _pushcnt_$1=0
  fi
  if test "$_total_pushcnt" = "" ; then
    _total_pushcnt=0
  fi
  if test "${$1+set}" = set; then
   _gasnet_pushvar_isset=1
  else
   _gasnet_pushvar_isset=0
  fi
  eval _pushedvar_$1_$_pushcnt_$1=\$[$1]
  eval _pushedvarset_$1_$_pushcnt_$1=$_gasnet_pushvar_isset
  _pushcnt_$1=`expr $_pushcnt_$1 + 1`
  _total_pushcnt=`expr $_total_pushcnt + 1`
  $1=$2
  echo "pushed new $1 value: $[$1]" >&5
  GASNET_FUN_END([$0($1,$2)])
]) 
dnl push a variable, then unset it
AC_DEFUN([GASNET_PUSHVAR_UNSET],[
  GASNET_FUN_BEGIN([$0($1)])
    GASNET_PUSHVAR($1,"<unset>") 
    unset $1
  GASNET_FUN_END([$0($1)])
])

dnl restore the old value of varname, from a previous push
dnl GASNET_POPVAR(varname)
AC_DEFUN([GASNET_POPVAR],[
  GASNET_FUN_BEGIN([$0($1)])
  if test "$_pushcnt_$1" -ge "1"; then
    _pushcnt_$1=`expr $_pushcnt_$1 - 1`
    _total_pushcnt=`expr $_total_pushcnt - 1`
    eval _gasnet_pushvar_isset=\$_pushedvarset_$1_$_pushcnt_$1
    if test "$_gasnet_pushvar_isset" = "1" ; then
      eval $1=\$_pushedvar_$1_$_pushcnt_$1
      echo "popping $1 back to: $[$1]" >&5
    else
      unset $1
      echo "popping $1 back to: <unset>" >&5
    fi
  else
    AC_MSG_ERROR([INTERNAL ERROR: GASNET_PUSH/POPVAR underflow on $1])
  fi
  GASNET_FUN_END([$0($1)])
]) 

AC_DEFUN([GASNET_PUSHPOP_CHECK],[
GASNET_FUN_BEGIN([$0])
  if test "$_total_pushcnt" -ge "1" ; then
    AC_MSG_ERROR([INTERNAL ERROR: GASNET_PUSH/POPVAR mismatch: $_total_pushcnt more pushes than pops])
  fi
GASNET_FUN_END([$0])
])

dnl add file to list of executable outputs that should be marked +x
dnl would be nice to use AC_CONFIG_COMMANDS() for each file, but autoconf 2.53
dnl  stupidly fails to execute commands having the same tag as a config output file
dnl  on subsequent calls to config.status
AC_DEFUN([GASNET_FIX_EXEC],[
GASNET_FUN_BEGIN([$0($1)])
  cv_prefix[]exec_list="$cv_prefix[]exec_list $1"
GASNET_FUN_END([$0($1)])
])

dnl ensure the "default" command is run on every invocation of config.status
AC_DEFUN([GASNET_FIX_EXEC_SETUP],[[
GASNET_FUN_BEGIN([$0])
  dnl round-about method ensure autoconf 2.53 picks up depfiles command
  if test "\${config_commands+set}" != set ; then
    config_commands="default"
  fi
  CONFIG_COMMANDS="\$config_commands"
  cv_prefix[]exec_list="$cv_prefix[]exec_list"
GASNET_FUN_END([$0])
]])

AC_DEFUN([GASNET_FIX_EXEC_OUTPUT],[[
GASNET_FUN_BEGIN([$0])
  for file in $cv_prefix[]exec_list; do
   case "$CONFIG_FILES" in
     *${file}*) chmod +x ${file} ;;
   esac
  done
GASNET_FUN_END([$0])
]])

AC_DEFUN([GASNET_LIBGCC],[
GASNET_FUN_BEGIN([$0])
AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(for libgcc link flags, cv_prefix[]lib_gcc,
[if test "$GCC" = yes; then
  #LIBGCC="`$CC -v 2>&1 | sed -n 's:^Reading specs from \(.*\)/specs$:-L\1 -lgcc:p'`"
  LIBGCC="-L`$CC -print-libgcc-file-name | xargs dirname` -lgcc"
  if test -z "$LIBGCC"; then
    AC_MSG_ERROR(cannot find libgcc)
  fi
fi
cv_prefix[]lib_gcc="$LIBGCC"])
LIBGCC="$cv_prefix[]lib_gcc"
AC_SUBST(LIBGCC)
GASNET_FUN_END([$0])
])

AC_DEFUN([GASNET_LIBM],[
GASNET_FUN_BEGIN([$0])
AC_REQUIRE([AC_PROG_CC])
GASNET_PUSHVAR(LIBS,"$LIBS")
case "$target_os" in
  darwin*)
    # libm is just an alias for the system default lib
    # Naming it explicitly causes linker failures when linking w/ mpich
  ;;
  *)
    # sin should be in everyone's libm if they've got one.
    AC_CHECK_LIB(m, sin, LIBM="-lm", LIBM="")
  ;;
esac
AC_SUBST(LIBM)
GASNET_POPVAR(LIBS)
GASNET_FUN_END([$0])
])

dnl GASNET_ENV_DEFAULT(envvar-name, default-value)
dnl  load an environment variable, using default value if it's missing from env.
dnl  caches the results to guarantee reconfig gets the originally loaded value
dnl  also adds a --with-foo-bar= option for the env variable FOO_BAR
AC_DEFUN([GASNET_ENV_DEFAULT],[
  GASNET_FUN_BEGIN([$0($1,$2)])
  pushdef([lowerdashname],patsubst(translit([$1],'A-Z','a-z'), _, -))
  pushdef([lowerscorename],patsubst(translit([$1],'A-Z','a-z'), -, _))
  
  AC_MSG_CHECKING(for $1 in environment)

  dnl create the help prompt just once
  ifdef(with_expanded_[$1], [], [
    AC_ARG_WITH(lowerdashname, 
       GASNET_OPTION_HELP(with-[]lowerdashname[]=, value for [$1]), 
      [], [])
  ])
  define(with_expanded_[$1], [set])

  envval_src_[$1]="cached"
  AC_CACHE_VAL(cv_prefix[]envvar_$1, [
      case "${[$1]-__NOT_SET__}" in
	__NOT_SET__) 
            if test "$with_[]lowerscorename" != ""; then
	      cv_prefix[]envvar_$1="$with_[]lowerscorename"
	      envval_src_[$1]=given
	    else
	      cv_prefix[]envvar_$1="[$2]"
	      envval_src_[$1]=default
	    fi 
	    ;;
	*)  cv_prefix[]envvar_$1="$[$1]"
	    envval_src_[$1]=given
      esac
  ])

  [$1]="$cv_prefix[]envvar_$1"
  case "$envval_src_[$1]" in
      'cached')
	  AC_MSG_RESULT([using cached value \"$[$1]\"]) ;;
      'default')
	  AC_MSG_RESULT([no, defaulting to \"$[$1]\"]) ;;
      'given')
	  AC_MSG_RESULT([yes, using \"$[$1]\"]) ;;
      *) AC_MSG_ERROR(_GASNET_ENV_DEFAULT broken)
  esac

  popdef([lowerdashname])
  popdef([lowerscorename])
  GASNET_FUN_END([$0($1,$2)])
])

dnl $1 = optional env variables to restore
AC_DEFUN([GASNET_START_CONFIGURE],[
  GASNET_FUN_BEGIN([$0($1)])
  GASNET_PATH_PROGS(PWD_PROG, pwd, pwd)

  dnl Save and display useful info about the configure environment
  GASNET_GET_AUTOCONF_VERSION()
  AC_MSG_CHECKING(for configure settings) 
  AC_MSG_RESULT([])
  CONFIGURE_ARGS="$ac_configure_args"
  AC_SUBST(CONFIGURE_ARGS)
  AC_MSG_RESULT( configure args: $CONFIGURE_ARGS)
  dnl ensure the cache is used in all reconfigures
  if test "$cache_file" = "/dev/null" ; then
    echo WARNING: configure cache_file setting got lost - you may need to run a fresh ./Bootstrap
    cache_file=config.cache
  fi
  ac_configure_args="$ac_configure_args --cache-file=$cache_file"
  dnl don't trust shell's builtin pwd, because it may include symlinks
  TOP_SRCDIR=`cd ${srcdir} && ${PWD_PROG}` 
  AC_MSG_RESULT( TOP_SRCDIR:     $TOP_SRCDIR)
  AC_SUBST(TOP_SRCDIR)
  TOP_BUILDDIR=`${PWD_PROG}`
  AC_MSG_RESULT( TOP_BUILDDIR:   $TOP_BUILDDIR)
  AC_SUBST(TOP_BUILDDIR)
  dnl check against bug 1083 (spaces in directory name break things)
  if `echo $TOP_SRCDIR | grep ' ' >/dev/null 2>/dev/null`; then
    AC_MSG_ERROR(TOP_SRCDIR contains space characters - please unpack the source in a different directory.)
  fi
  if `echo $TOP_BUILDDIR | grep ' ' >/dev/null 2>/dev/null`; then
    AC_MSG_ERROR(TOP_BUILDDIR contains space characters - please build in a different directory.)
  fi
  dnl set AM_CONDITIONAL BUILD_IS_SRC for ease of use in generated Makefiles
  AM_CONDITIONAL(BUILD_IS_SRC, test "$TOP_BUILDDIR" = "$TOP_SRCDIR")
  dnl set AC_SUBST variable BUILD_IS_SRC for ease of use in generated scripts
  if test "$TOP_BUILDDIR" = "$TOP_SRCDIR"; then
    BUILD_IS_SRC=yes
  else
    BUILD_IS_SRC=no
  fi
  AC_SUBST(BUILD_IS_SRC)
  SYSTEM_NAME="`hostname`"
  AC_SUBST(SYSTEM_NAME)
  SYSTEM_TUPLE="$target"
  AC_SUBST(SYSTEM_TUPLE)
  AC_MSG_RESULT( system info:      $SYSTEM_NAME $SYSTEM_TUPLE)
  BUILD_USER=`whoami 2> /dev/null || id -un 2> /dev/null || echo $USER`
  BUILD_ID="`date` $BUILD_USER"
  AC_MSG_RESULT( build id:       $BUILD_ID)
  AC_SUBST(BUILD_ID)

  GASNET_RESTORE_AUTOCONF_ENV([CC CXX CFLAGS CXXFLAGS CPPFLAGS LIBS MAKE GMAKE AR AS RANLIB PERL SUM LEX YACC $1])
  GASNET_FUN_END([$0($1)])
])

AC_DEFUN([GASNET_END_CONFIGURE],[
  GASNET_ERR_CLEANUP()
  GASNET_SAVE_AUTOCONF_ENV()
  GASNET_PUSHPOP_CHECK()
])

dnl AC_DEFINE the configure information variables detected by GASNET_START_CONFIGURE, with prefix
AC_DEFUN([GASNET_DEFINE_CONFIGURE_VARS],[
  GASNET_FUN_BEGIN([$0])
  AC_REQUIRE([GASNET_START_CONFIGURE])
  AC_DEFINE_UNQUOTED($1_[]CONFIGURE_ARGS, "$CONFIGURE_ARGS")
  AC_DEFINE_UNQUOTED($1_[]SYSTEM_NAME,    "$SYSTEM_NAME")
  AC_DEFINE_UNQUOTED($1_[]SYSTEM_TUPLE,   "$SYSTEM_TUPLE")
  AC_DEFINE_UNQUOTED($1_[]BUILD_ID,       "$BUILD_ID")
  GASNET_FUN_END([$0])
])

dnl GASNET_RESTORE_AUTOCONF_ENV(env1 env2 env3) 
dnl  call at top of configure.in to restore cached environment variables 
dnl  inspected by autoconf macros. Pass in names of variables
AC_DEFUN([GASNET_RESTORE_AUTOCONF_ENV],[
  GASNET_FUN_BEGIN([$0($1)])
  dnl  pushdef = get a variable prefix variable which won't be cached.
  pushdef([nc_prefix],patsubst(cv_prefix,_cv_,_))
  if test "$nc_prefix[]acenv_list" != ""; then
    AC_MSG_ERROR(_GASNET_RESTORE_AUTOCONF_ENV called more than once with prefix = "cv_prefix")
  fi
  nc_prefix[]acenv_list="$1"
  AC_MSG_CHECKING(for cached autoconf environment settings)
  AC_MSG_RESULT("") 
  for varname in $1; do
    val=`eval echo '$'"cv_prefix[]acenv_$varname"`
    if test "$val" != ""; then
      eval $varname=\"$val\"
      AC_MSG_RESULT([$varname=\"$val\"]) 
    fi
  done
  popdef([nc_prefix])
  GASNET_FUN_END([$0($1)])
])

dnl GASNET_SAVE_AUTOCONF_ENV() 
dnl  cache the environment variables inspected by autoconf macros
AC_DEFUN([GASNET_SAVE_AUTOCONF_ENV],[
  GASNET_FUN_BEGIN([$0])
  for varname in $cv_prefix[]acenv_list; do
    val=`eval echo '$'"$varname"`
    if test "$val" != ""; then
      cachevarname=cv_prefix[]acenv_$varname
      eval $cachevarname=\"$val\"
    fi
  done
  GASNET_FUN_END([$0])
])

dnl m4 substr fiasco:
dnl autoconf 2.13 has a working version of the m4 function 'substr', 
dnl  but no m4_substr (and no format or m4_format)
dnl autoconf 2.58 has working versions of m4_substr and m4_format, 
dnl  but no substr or format
dnl This incantation ensures m4_substr works regardless
ifdef([substr],[define([m4_substr], defn([substr]))])

AC_DEFUN([GASNET_OPTION_HELP],[  --$1 ]m4_substr[([                         ],len([$1]))[$2]])

dnl provide a --with-foo=bar configure option
dnl action-withval runs for a named value in $withval (or withval=yes if named arg missing)
dnl action-without runs for --without-foo or --with-foo=no
dnl action-none runs for no foo arg given
dnl GASNET_WITH(foo, description, action-withval, [action-without], [action-none])
AC_DEFUN([GASNET_WITH],[
GASNET_FUN_BEGIN([$0($1,...)])
AC_ARG_WITH($1,GASNET_OPTION_HELP(with-$1=value,[$2]), [
  case "$withval" in
    no) :
        $4 ;;
    *)  $3 ;;
  esac
  ],[
   :
   $5
  ])
GASNET_FUN_END([$0($1,...)])
])

AC_DEFUN([GASNET_IF_ENABLED_NOHELP],[
case "$enable_[]patsubst([$1], -, _)" in
  '' | no) :
      $3 ;;
  *)  $2 ;;
esac
])

AC_DEFUN([GASNET_IF_ENABLED],[
GASNET_FUN_BEGIN([$0($1,...)])
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,[$2]))
GASNET_IF_ENABLED_NOHELP([$1],[$3],[$4])
GASNET_FUN_END([$0($1,...)])
])

AC_DEFUN([GASNET_IF_DISABLED],[
GASNET_FUN_BEGIN([$0($1,...)])
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,[$2]))
case "$enable_[]patsubst([$1], -, _)" in
  '' | yes) :
       $4 ;;
  *)   $3 ;;
esac
GASNET_FUN_END([$0($1,...)])
])

AC_DEFUN([GASNET_IF_ENABLED_WITH_AUTO],[
GASNET_FUN_BEGIN([$0($1,...)])
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,[$2]))
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,[$2]))
case "$enable_[]patsubst([$1], -, _)" in
  no)  $4 ;;
  yes) $3 ;;
  *)   $5 ;;
esac
GASNET_FUN_END([$0($1,...)])
])

AC_DEFUN([GASNET_SUBST],[
$1="$2"
AC_SUBST($1)])

AC_DEFUN([GASNET_SUBST_FILE],[
$1="$2"
AC_SUBST_FILE($1)])

AC_DEFUN([GASNET_CHECK_PROGS],[
GASNET_FUN_BEGIN([$0($1,$2,$3)])
case "$$1" in
  '') AC_CHECK_PROGS($1,$2)
      ;;
  *) AC_MSG_CHECKING(for $3)
     AC_MSG_RESULT($$1)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac
GASNET_FUN_END([$0($1,$2,$3)])
])

AC_DEFUN([GASNET_PATH_PROGS],[
GASNET_FUN_BEGIN([$0($1,$2,$3)])
case "$$1" in
  '') AC_PATH_PROGS($1,$2)
      ;;
  *) AC_MSG_CHECKING(for $3)
     AC_MSG_RESULT($$1)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac
GASNET_FUN_END([$0($1,$2,$3)])
])

dnl GASNET_GETFULLPATH(var)
dnl var contains a program name, optionally followed by arguments
dnl expand the program name to a fully qualified pathname if not already done
AC_DEFUN([GASNET_GETFULLPATH_CHECK],[
GASNET_IF_DISABLED(full-path-expansion, [Disable expansion of program names to full pathnames], 
                   [cv_prefix[]_gfp_disable=1])
])
AC_DEFUN([GASNET_GETFULLPATH],[
GASNET_FUN_BEGIN([$0($1)])
AC_REQUIRE([AC_PROG_AWK])
AC_REQUIRE([GASNET_GETFULLPATH_CHECK])
if test "$cv_prefix[]_gfp_disable" = ""; then
  gasnet_gfp_progname=`echo "$$1" | $AWK -F' ' '{ print [$]1 }'`
  gasnet_gfp_progargs=`echo "$$1" | $AWK -F' ' 'BEGIN { ORS=" "; } { for (i=2;i<=NF;i++) print $i; }'`
  gasnet_gfp_progname0=`echo "$gasnet_gfp_progname" | $AWK '{ print sub[]str([$]0,1,1) }'`
  if test "$gasnet_gfp_progname0" != "/" ; then
    # clear cached values, in case this is a pushed var
    unset cv_prefix[]_gfp_fullprogname_$1
    unset ac_cv_path_[]cv_prefix[]_gfp_fullprogname_$1
    # [AC_PATH_PROG](cv_prefix[]_gfp_fullprogname_$1, $gasnet_gfp_progname,[])
    AC_PATH_PROG(cv_prefix[]_gfp_fullprogname_$1, $gasnet_gfp_progname,[])
    AC_MSG_CHECKING(for full path expansion of $1)
    if test "$cv_prefix[]_gfp_fullprogname_$1" != "" ; then
      $1="$cv_prefix[]_gfp_fullprogname_$1 $gasnet_gfp_progargs"
    fi
    AC_MSG_RESULT($$1)
  fi
fi
GASNET_FUN_END([$0($1)])
])

dnl GASNET_FOLLOWLINKS(var)
dnl var contains a filename
dnl If it is a symlink, follow it until a non-symlink is reached
dnl Designed not to use readlink, which might not exist.
AC_DEFUN([GASNET_FOLLOWLINKS],[
GASNET_FUN_BEGIN([$0($1)])
AC_REQUIRE([AC_PROG_AWK])
  gasnet_fl_file="$$1"
  gasnet_fl_link=`/bin/ls -al "$gasnet_fl_file" | $AWK 'BEGIN{FS=">"}{split([$]2,A," ") ; print A[[1]]}'`
  while test "$gasnet_fl_link"; do
    gasnet_fl_file="$gasnet_fl_link"
    gasnet_fl_link=`/bin/ls -al "$gasnet_fl_file" | $AWK 'BEGIN{FS=">"}{split([$]2,A," ") ; print A[[1]]}'`
  done
  $1="$gasnet_fl_file"
GASNET_FUN_END([$0($1)])
])

dnl GASNET_CHECK_LIB(library, function, action-if-found, action-if-not-found, other-flags, other-libraries)
AC_DEFUN([GASNET_CHECK_LIB],[
GASNET_FUN_BEGIN([$0($1,$2,...)])
GASNET_PUSHVAR(LDFLAGS,"$LDFLAGS $5")
AC_CHECK_LIB($1, $2, $3, $4, $6)
GASNET_POPVAR(LDFLAGS)
GASNET_FUN_END([$0($1,$2,...)])
])

dnl GASNET_TRY_RUNCMD(command, action-success-nooutput, action-success-output, action-error)
dnl run a command, and take action based on the result code and output (in $gasnet_cmd_stdout/$gasnet_cmd_stderr)
AC_DEFUN([GASNET_TRY_RUNCMD],[
  GASNET_FUN_BEGIN([$0($1,...)])
  echo \"$1\" >&5
  ( $1 ) > conftest-runcmdout 2> conftest-runcmderr
  gasnet_cmd_result="$?"
  gasnet_cmd_stdout="`cat conftest-runcmdout`"
  gasnet_cmd_stderr="`cat conftest-runcmderr`"
  cat conftest-runcmdout >&5
  cat conftest-runcmderr >&5
  echo gasnet_cmd_result=$gasnet_cmd_result >&5
  rm -rf conftest*
  if test "$gasnet_cmd_result" = "0" ; then 
    if test -z "$gasnet_cmd_stdout$gasnet_cmd_stderr" ; then
      :
      $2 
    else
      :
      $3 
    fi
  else
    :
    $4 
  fi
  GASNET_FUN_END([$0($1,...)])
])

dnl GASNET_TRY_CCOMPILE_WITHWARN(includes, function-body, action-success, action-warning, action-error)
dnl Compile a C program and take different actions based on complete success, error or warning
dnl Automatically handles compilers that issue unrelated warnings on every compile

AC_DEFUN([GASNET_TRY_CCOMPILE_WITHWARN],[
  GASNET_FUN_BEGIN([$0(...)])
  GASNET_TRY_CCOMPILE_WITHWARN_NORETRY([$1],[$2],[$3],[
    dnl got a warning - does same warning also happen with an empty program?
    _GASNET_TRY_COMPILE_WITHWARN_OUTTMP="$gasnet_cmd_stdout"
    _GASNET_TRY_COMPILE_WITHWARN_ERRTMP="$gasnet_cmd_stderr"
    GASNET_TRY_CCOMPILE_WITHWARN_NORETRY([],[],[
        dnl no warning on empty program => warning caused by input
	gasnet_cmd_stdout="$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP"
	gasnet_cmd_stderr="$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP"
    	$4
    ],[ dnl still got a warning - is the same?
      if test "$gasnet_cmd_stdout$gasnet_cmd_stderr" = "$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP" ; then
        dnl identical warnings => no new warnings caused by program
	$3
      else
        dnl different warnings => program is likely causal factor
	gasnet_cmd_stdout="$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP"
	gasnet_cmd_stderr="$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP"
	$4
      fi
    ],[ dnl got an error on an empty program!
      GASNET_MSG_ERROR([unknown failure case in TRY_CCOMPILE_WITHWARN])
    ])
  ],[$5])
  GASNET_FUN_END([$0(...)])
])

dnl for internal use only
AC_DEFUN([GASNET_TRY_CCOMPILE_WITHWARN_NORETRY],[
  GASNET_FUN_BEGIN([$0(...)])
  gasnet_testname=gasnet-conftest
  gasnet_testfile=${gasnet_testname}.c
  gasnet_compile_cmd="${CC-cc} -c $CFLAGS $CPPFLAGS $gasnet_testfile"
  cat > $gasnet_testfile <<EOF
#include "confdefs.h"
$1
int main() {
$2
; return 0; }
EOF
  GASNET_TRY_RUNCMD([$gasnet_compile_cmd], [$3], [
    echo "configure: warned program was:" >&5
    cat $gasnet_testfile >&5
    $4
    ],[
    echo "configure: failed program was:" >&5
    cat $gasnet_testfile >&5
    $5
    ])
  rm -f ${gasnet_testname}.*
  GASNET_FUN_END([$0(...)])
])

dnl GASNET_TRY_CXXCOMPILE_WITHWARN(includes, function-body, action-success, action-warning, action-error)
dnl Compile a C++ program and take different actions based on complete success, error or warning
dnl Automatically handles compilers that issue unrelated warnings on every compile

AC_DEFUN([GASNET_TRY_CXXCOMPILE_WITHWARN],[
  GASNET_FUN_BEGIN([$0(...)])
  GASNET_TRY_CXXCOMPILE_WITHWARN_NORETRY([$1],[$2],[$3],[
    dnl got a warning - does same warning also happen with an empty program?
    _GASNET_TRY_COMPILE_WITHWARN_OUTTMP="$gasnet_cmd_stdout"
    _GASNET_TRY_COMPILE_WITHWARN_ERRTMP="$gasnet_cmd_stderr"
    GASNET_TRY_CXXCOMPILE_WITHWARN_NORETRY([],[],[
        dnl no warning on empty program => warning caused by input
	gasnet_cmd_stdout="$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP"
	gasnet_cmd_stderr="$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP"
    	$4
    ],[ dnl still got a warning - is the same?
      if test "$gasnet_cmd_stdout$gasnet_cmd_stderr" = "$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP" ; then
        dnl identical warnings => no new warnings caused by program
	$3
      else
        dnl different warnings => program is likely causal factor
	gasnet_cmd_stdout="$_GASNET_TRY_COMPILE_WITHWARN_OUTTMP"
	gasnet_cmd_stderr="$_GASNET_TRY_COMPILE_WITHWARN_ERRTMP"
	$4
      fi
    ],[ dnl got an error on an empty program!
      GASNET_MSG_ERROR([unknown failure case in TRY_CXXCOMPILE_WITHWARN])
    ])
  ],[$5])
  GASNET_FUN_END([$0(...)])
])

dnl for internal use only
AC_DEFUN([GASNET_TRY_CXXCOMPILE_WITHWARN_NORETRY],[
  GASNET_FUN_BEGIN([$0(...)])
  gasnet_testname=gasnet-conftest
  gasnet_testfile=${gasnet_testname}.cc
  gasnet_compile_cmd="${CXX-c++} -c $CXXFLAGS $CPPFLAGS $gasnet_testfile"
  cat > $gasnet_testfile <<EOF
#include "confdefs.h"
$1
int main() {
$2
; return 0; }
EOF
  GASNET_TRY_RUNCMD([$gasnet_compile_cmd], [$3], [
    echo "configure: warned program was:" >&5
    cat $gasnet_testfile >&5
    $4
    ],[
    echo "configure: failed program was:" >&5
    cat $gasnet_testfile >&5
    $5
    ])
  rm -f ${gasnet_testname}.*
  GASNET_FUN_END([$0(...)])
])

dnl GASNET_TRY_CFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN([GASNET_TRY_CFLAG],[
GASNET_FUN_BEGIN([$0($1)])
GASNET_PUSHVAR(CFLAGS,"$CFLAGS $1")
AC_MSG_CHECKING(for C compiler flag $1)
GASNET_TRY_CCOMPILE_WITHWARN_NORETRY([], [], [
 AC_MSG_RESULT(yes)
 GASNET_POPVAR(CFLAGS)
 $2
], [
 dnl some compilers issue a warning on *every* compile, 
 dnl so save the warning and try again without the flag being tested, 
 dnl to verify the warning we saw is actually a new warning
 _GASNET_TRY_CFLAG_TMP="$gasnet_cmd_stdout$gasnet_cmd_stderr"
 GASNET_POPVAR(CFLAGS)
 GASNET_TRY_CCOMPILE_WITHWARN_NORETRY([], [], [
   dnl warning disappeared when flag removed => flag is the cause
   AC_MSG_RESULT(no/warning: $_GASNET_TRY_CFLAG_TMP)
   $3
 ],[ 
   if test "$gasnet_cmd_stdout$gasnet_cmd_stderr" = "$_GASNET_TRY_CFLAG_TMP" ; then
     dnl got same warning => flag does not create new warnings
     AC_MSG_RESULT(yes/persistent-warning: $_GASNET_TRY_CFLAG_TMP)
     $2
   else
     dnl warnings differ with and without flag => flag is probably a causal factor
     AC_MSG_RESULT(no/new-warning: $_GASNET_TRY_CFLAG_TMP)
     $3
   fi
 ],[ dnl got an error - should never happen?
   GASNET_MSG_ERROR([unknown failure case in TRY_CFLAG])
 ])
], [
 AC_MSG_RESULT(no/error: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 GASNET_POPVAR(CFLAGS)
 $3
])
GASNET_FUN_END([$0($1)])
])

dnl GASNET_TRY_CXXFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN([GASNET_TRY_CXXFLAG],[
GASNET_FUN_BEGIN([$0($1)])
GASNET_PUSHVAR(CXXFLAGS,"$CXXFLAGS $1")
AC_MSG_CHECKING(for C++ compiler flag $1)
GASNET_TRY_CXXCOMPILE_WITHWARN_NORETRY([], [], [
 AC_MSG_RESULT(yes)
 GASNET_POPVAR(CXXFLAGS)
 $2
], [
 dnl some compilers issue a warning on *every* compile, 
 dnl so save the warning and try again without the flag being tested, 
 dnl to verify the warning we saw is actually a new warning
 _GASNET_TRY_CXXFLAG_TMP="$gasnet_cmd_stdout$gasnet_cmd_stderr"
 GASNET_POPVAR(CXXFLAGS)
 GASNET_TRY_CCOMPILE_WITHWARN_NORETRY([], [], [
   dnl warning disappeared when flag removed => flag is the cause
   AC_MSG_RESULT(no/warning: $_GASNET_TRY_CXXFLAG_TMP)
   $3
 ],[ 
   if test "$gasnet_cmd_stdout$gasnet_cmd_stderr" = "$_GASNET_TRY_CXXFLAG_TMP" ; then
     dnl got same warning => flag does not create new warnings
     AC_MSG_RESULT(yes/persistent-warning: $_GASNET_TRY_CXXFLAG_TMP)
     $2
   else
     dnl warnings differ with and without flag => flag is probably a causal factor
     AC_MSG_RESULT(no/new-warning: $_GASNET_TRY_CXXFLAG_TMP)
     $3
   fi
 ],[ dnl got an error - should never happen?
   GASNET_MSG_ERROR([unknown failure case in TRY_CXXFLAG])
 ])
], [
 AC_MSG_RESULT(no/error: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 GASNET_POPVAR(CXXFLAGS)
 $3
])
GASNET_FUN_END([$0($1)])
])

dnl GASNET_SET_CHECKED_CFLAGS CCVAR CFLAGSVAR DEFAULT_CFLAGS SAFE_CFLAGS
dnl Set CFLAGSVAR to a values that works with CCVAR 
dnl if CFLAGSVAR is already set, then keep it
dnl otherwise, if DEFAULT_CFLAGS works, then use it
dnl otherwise, use SAFE_CFLAGS
AC_DEFUN([GASNET_SET_CHECKED_CFLAGS],[
GASNET_FUN_BEGIN([$0(...)])
if test "$[$2]" != "" ; then
  GASNET_ENV_DEFAULT([$2], []) # user-provided flags
else
  GASNET_ENV_DEFAULT([$2], [$3]) # try DEFAULT_CFLAGS
  GASNET_PUSHVAR(CC,"$[$1]")
  GASNET_PUSHVAR(CFLAGS,"")
    GASNET_TRY_CFLAG([$[$2]], [], [
	AC_MSG_WARN([Unable to use default $2="$[$2]" so using "$4" instead. Consider manually seting $2])
        $2="$4"
    ])
  GASNET_POPVAR(CC)
  GASNET_POPVAR(CFLAGS)
fi
GASNET_FUN_END([$0(...)])
])

dnl GASNET_CHECK_OPTIMIZEDDEBUG CCVAR CFLAGSVAR EXTRAARGS INCLUDES 
dnl Ensure the compiler CC doesn't create a conflict between
dnl optimization and debugging.
AC_DEFUN([GASNET_CHECK_OPTIMIZEDDEBUG],[
GASNET_FUN_BEGIN([$0(...)])
 if test "$enable_debug" = "yes" ; then
  AC_MSG_CHECKING([$1 for debug vs. optimize compilation conflict])
  AC_LANG_SAVE
  AC_LANG_C
  GASNET_PUSHVAR(CC,"$[$1]")
  GASNET_PUSHVAR(CFLAGS,"$[$2] $3")
  AC_TRY_COMPILE( [
    $4
    #if defined(__OPTIMIZE__) || defined(NDEBUG)
	choke me
    #endif
  ], [ ], [ AC_MSG_RESULT(no) ], [
    AC_MSG_RESULT([yes])
    GASNET_MSG_ERROR([User requested --enable-debug but $1 or $2 has enabled optimization (-O) or disabled assertions (-DNDEBUG). Try setting $1='$[$1] -O0 -UNDEBUG' or changing $2])
  ])
  GASNET_POPVAR(CC)
  GASNET_POPVAR(CFLAGS)
  AC_LANG_RESTORE
 fi
GASNET_FUN_END([$0(...)])
])

dnl Checks if 'restrict' C99 keyword (or variants) supported
dnl #defines GASNETI_RESTRICT to correct variant, or to nothing
AC_DEFUN([GASNET_CHECK_RESTRICT],[
GASNET_FUN_BEGIN([$0])
  dnl Check for restrict keyword
  restrict_keyword=""
  if test "$restrict_keyword" = ""; then
    GASNET_TRY_CACHE_CHECK(for restrict keyword, cc_keyrestrict,
      [int dummy(void * restrict p) { return 1; }], [],
      restrict_keyword="restrict")
  fi
  if test "$restrict_keyword" = ""; then
    GASNET_TRY_CACHE_CHECK(for __restrict__ keyword, cc_key__restrict__,
      [int dummy(void * __restrict__ p) { return 1; }], [],
      restrict_keyword="__restrict__")
  fi
  if test "$restrict_keyword" = ""; then
    GASNET_TRY_CACHE_CHECK(for __restrict keyword, cc_key__restrict,
      [int dummy(void * __restrict p) { return 1; }], [],
      restrict_keyword="__restrict")
  fi
  AC_DEFINE_UNQUOTED(GASNETI_RESTRICT, $restrict_keyword)
  GASNET_TRY_CACHE_CHECK(whether restrict may qualify typedefs, cc_restrict_typedefs,
    [typedef void *foo_t;
     int dummy(foo_t GASNETI_RESTRICT p) { return 1; }], [],
    AC_DEFINE(GASNETI_RESTRICT_MAY_QUALIFY_TYPEDEFS))
GASNET_FUN_END([$0])
])

dnl check whether a given gcc attribute is available
dnl GASNET_CHECK_GCC_ATTRIBUTE(attribute-name, declaration, code)
AC_DEFUN([GASNET_CHECK_GCC_ATTRIBUTE],[
  GASNET_FUN_BEGIN([$0($1)])
  pushdef([uppername],translit(patsubst([$1], [_], []),'a-z','A-Z'))
  AC_MSG_CHECKING(for __attribute__(($1)))
  GASNET_TRY_CCOMPILE_WITHWARN([$2], [$3], [
      AC_MSG_RESULT(yes)
      AC_DEFINE(GASNETI_HAVE_GCC_ATTRIBUTE_[]uppername)
      AC_DEFINE(GASNETI_HAVE_GCC_ATTRIBUTE)
    ],[ dnl AC_MSG_RESULT([no/warning: $gasnet_cmd_stdout$gasnet_cmd_stderr])
        AC_MSG_RESULT([no/warning])
    ],[ dnl AC_MSG_RESULT([no/error: $gasnet_cmd_stdout$gasnet_cmd_stderr]) 
        AC_MSG_RESULT([no/error]) 
  ])
  GASNET_FUN_END([$0($1)])
  popdef([uppername])
]) 

dnl Output compilation error information, if available and do a AC_MSG_ERROR
dnl should be used within the failed branch of the compile macro, otherwise
dnl use GASNET_ERR_SAVE() in the failed branch to save the error info
AC_DEFUN([GASNET_MSG_ERROR],[
echo
echo "configure error: $1"
if test "" ; then
if test -f "conftest.$ac_ext" ; then
  errfile=conftest.$ac_ext
else
  errfile=gasnet_errsave_file
fi
if test -f "$errfile" ; then
  echo
  echo " --- Failed program --- "
  cat $errfile
  echo " -----------------------"
fi
fi
if test -f "conftest.err" ; then
  errfile=conftest.err
else
  errfile=gasnet_errsave_err
fi
if test -f "$errfile" ; then
  echo
  echo "Compilation error: "
  echo
  cat $errfile
fi
echo
CONFIG_FILE=`pwd`/config.log
AC_MSG_ERROR(See $CONFIG_FILE for details.)
])

AC_DEFUN([GASNET_ERR_SAVE],[
if test -f "conftest.$ac_ext" ; then
  cp conftest.$ac_ext gasnet_errsave_file
fi
if test -f "conftest.err" ; then
  cp conftest.err gasnet_errsave_err
fi
])

AC_DEFUN([GASNET_ERR_CLEANUP],[
  rm -f gasnet_errsave_file gasnet_errsave_err
])

dnl compile a program for a success/failure
dnl GASNET_TRY_CACHE_CHECK(description,cache_name,includes,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_CHECK],[
GASNET_FUN_BEGIN([$0($1,$2,...)])
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_COMPILE([$3], [$4], cv_prefix[]$2=yes, cv_prefix[]$2=no))
if test "$cv_prefix[]$2" = yes; then
  :
  $5
else
  :
  $6
fi
GASNET_FUN_END([$0($1,$2,...)])
])


dnl link a program for a success/failure
dnl GASNET_TRY_CACHE_LINK(description,cache_name,includes,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_LINK],[
GASNET_FUN_BEGIN([$0($1,$2,...)])
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_LINK([$3], [$4], cv_prefix[]$2=yes, cv_prefix[]$2=no))
if test "$cv_prefix[]$2" = yes; then
  :
  $5
else
  :
  $6
fi
GASNET_FUN_END([$0($1,$2,...)])
])

dnl run a program for a success/failure
dnl GASNET_TRY_CACHE_RUN(description,cache_name,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_RUN],[
GASNET_FUN_BEGIN([$0($1,$2,...)])
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_RUN([$3], cv_prefix[]$2=yes, cv_prefix[]$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$cv_prefix[]$2" = yes; then
  :
  $4
else
  :
  $5
fi
GASNET_FUN_END([$0($1,$2,...)])
])

dnl compile and run a program, error out if one fails (cross-compilation will skip the run)
dnl GASNET_TRY_CACHE_VERIFY_RUN(description,cache_name,includes,program,errormsg-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_VERIFY_RUN],[
  GASNET_FUN_BEGIN([$0($1,$2,...)])
  if test "$cross_compiling" = "yes" ; then
    GASNET_TRY_CACHE_LINK([$1],[$2],[$3],[$4],[ ],[ GASNET_MSG_ERROR([$5]) ])
  else
    GASNET_TRY_CACHE_RUN([$1],[$2],[
      $3
      int main() {
        $4
      }
    ],[ ],[ GASNET_MSG_ERROR([$5]) ])
  fi
  GASNET_FUN_END([$0($1,$2,...)])
])

dnl run a program to extract the value of a runtime expression 
dnl the provided code should set the integer val to the relevant value
dnl GASNET_TRY_CACHE_RUN_EXPR(description,cache_name,headers,code_to_set_val,result_variable)
AC_DEFUN([GASNET_TRY_CACHE_RUN_EXPR],[
GASNET_FUN_BEGIN([$0($1,$2,...)])
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_RUN([
  #include "confdefs.h"
  #include <stdio.h>
  #include <stdlib.h>
  $3
  int main() {
    FILE *f=fopen("conftestval", "w");
    int val = 0;
    if (!f) exit(1);
    { $4; }
    fprintf(f, "%d\n", (int)(val));
    return 0;
  }], cv_prefix[]$2=`cat conftestval`, cv_prefix[]$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$cv_prefix[]$2" != no; then
  :
  $5=$cv_prefix[]$2
fi
GASNET_FUN_END([$0($1,$2,...)])
])

AC_DEFUN([GASNET_PROG_CPP], [
  GASNET_FUN_BEGIN([$0])
  AC_PROVIDE([$0])
  AC_PROG_CC
  AC_PROG_CPP
  GASNET_GETFULLPATH(CPP)
  AC_SUBST(CPP)
  AC_SUBST(CPPFLAGS)
  AC_MSG_CHECKING(for working C preprocessor)
  AC_LANG_SAVE
  AC_LANG_C
  gasnet_progcpp_extrainfo=
  dnl deal with preprocessors who foolishly return success exit code even when they saw #error
  if test -n "`$CPP -version 2>&1 < /dev/null | grep MIPSpro`" ; then
    dnl The MIPSPro compiler has a broken preprocessor exit code by default, fix it
    dnl Using this flag is preferable to ensure that #errors encountered during compilation are fatal
    gasnet_progcpp_extrainfo=" (added -diag_error 1035 to deal with broken MIPSPro preprocessor)"
    CFLAGS="$CFLAGS -diag_error 1035"
    CPPFLAGS="$CPPFLAGS -diag_error 1035"    
  fi
  dnl final check
  AC_TRY_CPP([
    # error
  ], [AC_MSG_ERROR(Your C preprocessor is broken - reported success when it should have failed)], [])
  AC_TRY_CPP([], [], [GASNET_MSG_ERROR(Your C preprocessor is broken - reported failure when it should have succeeded)])
  AC_TRY_CPP([
    #ifdef __cplusplus
      #error __cplusplus should not be defined in a C preprocessor!
    #endif
  ], [], [GASNET_MSG_ERROR([Your C preprocessor is broken, it erroneously defines __cplusplus. This software requires a true, working ANSI C compiler - a C++ compiler is not an acceptable replacement.])])
  AC_MSG_RESULT(yes$gasnet_progcpp_extrainfo)
  if test "$CPP" = "/lib/cpp" ; then
    badlibcppmsg="Autoconf detected your preprocessor to be '/lib/cpp' instead of '$CC -E'. This is almost always a mistake, resulting from either a broken C compiler or an outdated version of autoconf. Proceeding is very likely to result in incorrect configure decisions."
    GASNET_IF_ENABLED(allow-libcpp, Allow the use of /lib/cpp for preprocessing, [
      AC_MSG_WARN([$badlibcppmsg])
    ],[
      AC_MSG_ERROR([$badlibcppmsg \
        You may enable use of this preprocessor at your own risk by passing the --enable-allow-libcpp flag.])
    ])
  fi
  AC_LANG_RESTORE
  GASNET_FUN_END([$0])
])

AC_DEFUN([GASNET_PROG_CXXCPP], [
  GASNET_FUN_BEGIN([$0])
  AC_PROVIDE([$0])
  AC_PROG_CXX
  AC_PROG_CXXCPP
  GASNET_GETFULLPATH(CXXCPP)
  AC_SUBST(CXXCPP)
  AC_SUBST(CXXCPPFLAGS)
  AC_MSG_CHECKING(for working C++ preprocessor)
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  gasnet_progcxxcpp_extrainfo=
  dnl deal with preprocessors who foolishly return success exit code even when they saw #error
  if test -n "`$CXXCPP -version 2>&1 < /dev/null | grep MIPSpro`" ; then
    dnl The MIPSPro compiler has a broken preprocessor exit code by default, fix it
    dnl Using this flag is preferable to ensure that #errors encountered during compilation are fatal
    gasnet_progcxxcpp_extrainfo=" (added -diag_error 1035 to deal with broken MIPSPro preprocessor)"
    CXXFLAGS="$CXXFLAGS -diag_error 1035"
    CXXCPPFLAGS="$CXXCPPFLAGS -diag_error 1035"    
  fi
  dnl final check
  AC_TRY_CPP([
    # error
  ], [AC_MSG_ERROR(Your C++ preprocessor is broken - reported success when it should have failed)], [])
  AC_TRY_CPP([], [], [GASNET_MSG_ERROR(Your C++ preprocessor is broken - reported failure when it should have succeeded)])
  AC_TRY_CPP([
    #ifndef __cplusplus
      #error __cplusplus must be defined in a C++ preprocessor!
    #endif
  ], [], [GASNET_MSG_ERROR([Your C++ preprocessor is broken, it fails to define __cplusplus.])])
  AC_MSG_RESULT(yes$gasnet_progcxxcpp_extrainfo)
  AC_LANG_RESTORE
  GASNET_FUN_END([$0])
])

AC_DEFUN([GASNET_PROG_CC], [
  GASNET_FUN_BEGIN([$0])
  GASNET_PROG_CPP
  GASNET_GETFULLPATH(CC)
  AC_SUBST(CC)
  AC_SUBST(CFLAGS)
  AC_MSG_CHECKING(for working C compiler)
  AC_LANG_SAVE
  AC_LANG_C
  AC_TRY_COMPILE([], [
    fail for me
  ], [AC_MSG_ERROR(Your C compiler is broken - reported success when it should have failed)], [])
  AC_TRY_COMPILE([ #include <stdio.h>
                   #include <stdlib.h>
		 ], [ printf("hi\n"); exit(0); ], 
     [], [GASNET_MSG_ERROR(Your C compiler is broken - reported failure when it should have succeeded)])
  AC_TRY_COMPILE([
    double *p;
    void *foo(double *d) { 
      return d; 
    }
  ], [
    double d;
    /* (void *) is compatible with any pointer type in a C program */
    p = foo((void *)&d);
  ], [], [GASNET_MSG_ERROR([Your C compiler is broken, it fails to compile a simple C program using implicit void* conversion. This software requires a true, working ANSI C compiler - note that a C++ compiler is not an acceptable replacement.])])
  AC_TRY_LINK([ extern int some_bogus_nonexistent_symbol(); ], [ int x = some_bogus_nonexistent_symbol(); ],
              [AC_MSG_ERROR(Your C linker is broken - reported success when it should have failed)], [])
  AC_TRY_LINK([ #include <stdio.h>
                #include <stdlib.h>
              ], [ printf("hi\n"); exit(0); ], 
     [], [GASNET_MSG_ERROR(Your C link is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes)
  AC_MSG_CHECKING(if user enabled cross-compile)
  GASNET_IF_ENABLED(cross-compile, [ Enable cross-compilation (experimental) ], [
    AC_MSG_RESULT(yes)
    cross_compiling=yes 
    CROSS_COMPILING=1
    ac_cv_prog_cc_cross=yes 
  ], [
    dnl reset autoconf cross compilation setting, which is wrong if executables are broken
    AC_MSG_RESULT(no)
    cross_compiling=no
    CROSS_COMPILING=0
    ac_cv_prog_cc_cross=no
    AC_MSG_CHECKING([working C compiler executables])
    AC_TRY_RUN([int main() { return 0; }], [AC_MSG_RESULT(yes)],
  	     [AC_MSG_RESULT(no) GASNET_MSG_ERROR([Cannot run executables created with C compiler. If you're attempting to cross-compile, use --enable-cross-compile])], 
  	     [AC_MSG_ERROR(Internal configure error - please report)])
  ])
  AM_CONDITIONAL(CROSS_COMPILING, test "$cross_compiling" = "yes")
  AC_SUBST(CROSS_COMPILING)
  AC_LANG_RESTORE
  GASNET_FUN_END([$0])
])

AC_DEFUN([GASNET_PROG_CXX], [
  GASNET_FUN_BEGIN([$0])
  GASNET_PROG_CXXCPP
  GASNET_GETFULLPATH(CXX)
  AC_SUBST(CXX)
  AC_SUBST(CXXFLAGS)
  AC_MSG_CHECKING(for working C++ compiler)
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  AC_TRY_COMPILE([], [
    fail for me
  ], [AC_MSG_ERROR(Your C++ compiler is broken - reported success when it should have failed)], [])
  AC_TRY_COMPILE([ #include <stdio.h>
                   #include <stdlib.h>
                 ], [ printf("hi\n"); exit(0); ], 
     [], [GASNET_MSG_ERROR(Your C++ compiler is broken - reported failure when it should have succeeded)])
  AC_TRY_LINK([ extern int some_bogus_nonexistent_symbol(); ], [ int x = some_bogus_nonexistent_symbol(); ],
              [AC_MSG_ERROR(Your C++ linker is broken - reported success when it should have failed)], [])
  AC_TRY_LINK([ #include <stdio.h>
                   #include <stdlib.h>
              ], [ printf("hi\n"); exit(0); ], 
     [], [GASNET_MSG_ERROR(Your C++ link is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes)
  dnl reset autoconf cross compilation setting, which is wrong if executables are broken
  AC_MSG_CHECKING(if user enabled cross-compile)
  GASNET_IF_ENABLED(cross-compile, [ Enable cross-compilation (experimental) ], [
    AC_MSG_RESULT(yes)
    cross_compiling=yes 
    ac_cv_prog_cxx_cross=yes 
  ], [
    dnl reset autoconf cross compilation setting, which is wrong if executables are broken
    AC_MSG_RESULT(no)
    cross_compiling=no
    ac_cv_prog_cxx_cross=no
    AC_MSG_CHECKING([working C++ compiler executables])
    AC_TRY_RUN([int main() { return 0; }], [AC_MSG_RESULT(yes)],
  	     [AC_MSG_RESULT(no) GASNET_MSG_ERROR([Cannot run executables created with C++ compiler. If you're attempting to cross-compile, use --enable-cross-compile])], 
  	     [AC_MSG_ERROR(Internal configure error - please report)])
  ])
  AC_LANG_RESTORE
  GASNET_FUN_END([$0])
])

dnl fetch the host C compiler
AC_DEFUN([GASNET_PROG_HOSTCC], [
GASNET_FUN_BEGIN([$0])
if test "$cross_compiling" = "yes" ; then
  HOST_MSG="When cross-compiling, \$HOST_CC or --with-host-cc= must be set to indicate a C compiler for the host machine (ie the machine running this configure script)"
  GASNET_ENV_DEFAULT(HOST_CC, )
  GASNET_ENV_DEFAULT(HOST_CFLAGS, )
  GASNET_ENV_DEFAULT(HOST_LDFLAGS, )
  GASNET_ENV_DEFAULT(HOST_LIBS, )
  AC_SUBST(HOST_CC)
  AC_SUBST(HOST_CFLAGS)
  AC_SUBST(HOST_LDFLAGS)
  AC_SUBST(HOST_LIBS)
  if test ! "$HOST_CC" ; then
    AC_MSG_ERROR([$HOST_MSG])
  fi
  GASNET_PUSHVAR(CC,"$HOST_CC")
  GASNET_PUSHVAR(CFLAGS,"$HOST_CFLAGS")
  GASNET_PUSHVAR(LDFLAGS,"$HOST_LDFLAGS")
  GASNET_PUSHVAR(LIBS,"$HOST_LIBS")
  dnl push all the other goop that AC_PROG_C(PP) caches away
  GASNET_PUSHVAR_UNSET(CPP)
  GASNET_PUSHVAR_UNSET(CPPFLAGS)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_CC)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_CPP)
  GASNET_PUSHVAR_UNSET(ac_cv_c_compiler_gnu)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_cc_g)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_cc_stdc)
    GASNET_PROG_CC
    AC_LANG_SAVE
    AC_LANG_C
    GASNET_PUSHVAR(cross_compiling,"no")
    AC_MSG_CHECKING([working host C compiler executables])
    AC_TRY_RUN([int main() { return 0; }], [AC_MSG_RESULT(yes)],
             [AC_MSG_RESULT(no) GASNET_MSG_ERROR($HOST_MSG)],
             [AC_MSG_ERROR(Internal configure error - please report)])
    GASNET_POPVAR(cross_compiling)
    HOST_CC="$CC"
    HOST_CPP="$CPP"
    HOST_CPPFLAGS="$CPPFLAGS"
    HOST_CFLAGS="$CFLAGS"
    HOST_LDFLAGS="$LDFLAGS"
    HOST_LIBS="$LIBS"
    AC_LANG_RESTORE
  GASNET_POPVAR(CC)
  GASNET_POPVAR(CFLAGS)
  GASNET_POPVAR(LDFLAGS)
  GASNET_POPVAR(LIBS)
  GASNET_POPVAR(CPP)
  GASNET_POPVAR(CPPFLAGS)
  GASNET_POPVAR(ac_cv_prog_CC)
  GASNET_POPVAR(ac_cv_prog_CPP)
  GASNET_POPVAR(ac_cv_c_compiler_gnu)
  GASNET_POPVAR(ac_cv_prog_cc_g)
  GASNET_POPVAR(ac_cv_prog_cc_stdc)
fi
GASNET_FUN_END([$0])
])

dnl fetch the host C++ compiler
dnl this is a two part macro which must be called one after the other at the top level
dnl in order to avoid some annoying bugs in autoconf AC_REQUIRE 
AC_DEFUN([GASNET_PROG_HOSTCXX], [
GASNET_FUN_BEGIN([$0])
if test "$cross_compiling" = "yes" ; then
  HOST_MSG="When cross-compiling, \$HOST_CXX or --with-host-cxx= must be set to indicate a C++ compiler for the host machine (ie the machine running this configure script)"
  GASNET_ENV_DEFAULT(HOST_CXX, )
  GASNET_ENV_DEFAULT(HOST_CXXFLAGS, )
  GASNET_ENV_DEFAULT(HOST_CXX_LDFLAGS, )
  GASNET_ENV_DEFAULT(HOST_CXX_LIBS, )
  AC_SUBST(HOST_CXX)
  AC_SUBST(HOST_CXXFLAGS)
  AC_SUBST(HOST_CXX_LDFLAGS)
  AC_SUBST(HOST_CXX_LIBS)
  if test ! "$HOST_CXX" ; then
    AC_MSG_ERROR([$HOST_MSG])
  fi
  GASNET_PUSHVAR(CXX,"$HOST_CXX")
  GASNET_PUSHVAR(CXXFLAGS,"$HOST_CXXFLAGS")
  GASNET_PUSHVAR(LDFLAGS,"$HOST_CXX_LDFLAGS")
  GASNET_PUSHVAR(LIBS,"$HOST_CXX_LIBS")
  dnl push all the other goop that AC_PROG_CXX(CPP) caches away
  GASNET_PUSHVAR_UNSET(CXXCPP)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_CXX)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_CXXCPP)
  GASNET_PUSHVAR_UNSET(ac_cv_cxx_compiler_gnu)
  GASNET_PUSHVAR_UNSET(ac_cv_prog_cxx_g)
    GASNET_PROG_CXX
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    GASNET_PUSHVAR(cross_compiling,"no")
    AC_MSG_CHECKING([working host CXX compiler executables])
    AC_TRY_RUN([int main() { return 0; }], [AC_MSG_RESULT(yes)],
             [AC_MSG_RESULT(no) GASNET_MSG_ERROR($HOST_MSG)],
             [AC_MSG_ERROR(Internal configure error - please report)])
    GASNET_POPVAR(cross_compiling)
    HOST_CXX="$CXX"
    HOST_CXXCPP="$CXXCPP"
    HOST_CXXFLAGS="$CXXFLAGS"
    HOST_CXX_LDFLAGS="$LDFLAGS"
    HOST_CXX_LIBS="$LIBS"
    AC_LANG_RESTORE
  GASNET_POPVAR(CXX)
  GASNET_POPVAR(CXXFLAGS)
  GASNET_POPVAR(CXXCPP)
  GASNET_POPVAR(LDFLAGS)
  GASNET_POPVAR(LIBS)
  GASNET_POPVAR(ac_cv_prog_CXX)
  GASNET_POPVAR(ac_cv_prog_CXXCPP)
  GASNET_POPVAR(ac_cv_cxx_compiler_gnu)
  GASNET_POPVAR(ac_cv_prog_cxx_g)
fi
GASNET_FUN_END([$0])
])

dnl find working version of perl.  Checks to see if 'bytes' module is available,
dnl and sets GASNET_PERL_BYTESFLAG to either '-Mbytes' or empty string, for
dnl scripts that need to ward off Perl/UTF-8 issues 
AC_DEFUN([GASNET_PROG_PERL],[
  GASNET_FUN_BEGIN([$0])
  GASNET_PATH_PROGS(PERL, perl5 perl, perl)
  MIN_PERL_VERSION="5.005"
  AC_MSG_CHECKING(for perl version $MIN_PERL_VERSION or later)
  if $PERL -e "require $MIN_PERL_VERSION;" 2>/dev/null; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_ERROR(cannot find perl $MIN_PERL_VERSION or later)
  fi
  if $PERL -Mbytes -e "exit 0" 2>/dev/null; then
    GASNET_PERL_BYTESFLAG="-Mbytes"
  else
    GASNET_PERL_BYTESFLAG=
  fi
  AC_SUBST(GASNET_PERL_BYTESFLAG)
  GASNET_FUN_END([$0])
])

AC_DEFUN([GASNET_IFDEF],[
GASNET_FUN_BEGIN([$0($1)])
AC_TRY_CPP([
#ifndef $1
# error
#endif], [$2], [$3])
GASNET_FUN_END([$0($1)])
])


AC_DEFUN([GASNET_FAMILY_CACHE_CHECK],[
GASNET_FUN_BEGIN([$0])
AC_REQUIRE_CPP
AC_CACHE_CHECK(for $1 compiler family, $3, [
  $3=unknown

  dnl start with compilers having very slow preprocessors
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__xlC__, $3=XLC)
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(_CRAYC, $3=Cray)
  fi
  dnl gcc-like compilers, which may define __GNUC__ - order matters here
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__GNUC__, $3=GNU) 
    dnl Note GNUC one above must precede many of those below
    GASNET_IFDEF(__PATHCC__, $3=Pathscale)
    GASNET_IFDEF(__PGI, $3=PGI)
    GASNET_IFDEF(__INTEL_COMPILER, $3=Intel)
  fi
  dnl other vendor compilers
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__DECC, $3=Compaq) # Compaq C
    GASNET_IFDEF(__DECCXX, $3=Compaq) # Compaq C++
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__SUNPRO_C, $3=Sun)  # Sun C
    GASNET_IFDEF(__SUNPRO_CC, $3=Sun) # Sun C++
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__HP_cc, $3=HP)  # HP C
    GASNET_IFDEF(__HP_aCC, $3=HP) # HP aCC (C++)
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(_SGI_COMPILER_VERSION, $3=MIPS)
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__MTA__, $3=MTA)
  fi
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(__KCC, $3=KAI)
  fi

  dnl compilers lacking specific identifying marks - identify by platform
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(_SX, $3=NEC)
  fi
])
if test "$$3" != "GNU" ; then
  dnl Some compilers (eg Intel 8.0) define __GNUC__ even though they are definitely not GNU C
  dnl Don't believe their filthy lies
  case $2 in 
    CC) ac_cv_c_compiler_gnu=no
        GCC=""
    ;;
    CXX) ac_cv_cxx_compiler_gnu=no
        GXX=""
    ;;
  esac
fi
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
GASNET_FUN_END([$0])
])


dnl deal with a buggy version of autoconf which assumes alloca returns char *
AC_DEFUN([GASNET_FUNC_ALLOCA_HELPER],[
  patsubst([$*], [p = alloca], [p = (char *)alloca])
])

AC_DEFUN([GASNET_FUNC_ALLOCA],[
  AC_SUBST(ALLOCA)
  GASNET_FUNC_ALLOCA_HELPER(AC_FUNC_ALLOCA)
])

dnl Set command for use in Makefile.am to install various files
dnl This command should remove all the magic used to run from the build
dnl directory, as well as deal with setting of the prefix at install time.
AC_DEFUN([GASNET_SET_INSTALL_CMD],[
GASNET_INSTALL_CMD="sed -e '/###NOINSTALL###/d' -e 's@###INSTALL_PREFIX###@\$(prefix)@g'"
AC_SUBST(GASNET_INSTALL_CMD)
])

dnl pass $1 to all subconfigures invoked recursively from this configure script
AC_DEFUN([GASNET_SUBCONFIGURE_ARG],[
GASNET_FUN_BEGIN([$0($1)])
ac_configure_args="$ac_configure_args $1"
GASNET_FUN_END([$0($1)])
])

dnl fetch a cross-compilation variable, if we are cross compiling
dnl GASNET_CROSS_VAR(variable-to-set, basicname)
AC_DEFUN([GASNET_CROSS_VAR],[
  GASNET_FUN_BEGIN([$0($1,$2)])
  pushdef([cross_varname],CROSS_$2)
  if test "$cross_compiling" = "yes" ; then
    GASNET_ENV_DEFAULT(cross_varname,)
    if test "$cross_varname" = "" ; then
      AC_MSG_ERROR([This configure script requires \$cross_varname be set for cross-compilation])
    else 
      $1="$cross_varname"
    fi
  fi
  popdef([cross_varname])
  GASNET_FUN_END([$0($1,$2)])
])

dnl query the numerical value of a system signal and AC_SUBST it
AC_DEFUN([GASNET_GET_SIG], [
  GASNET_FUN_BEGIN([$0])
  if test "$cross_compiling" = "yes" ; then
    GASNET_CROSS_VAR(SIG$1,SIG$1)
  else 
    GASNET_TRY_CACHE_RUN_EXPR([value of SIG$1], SIG$1, [#include <signal.h>], [val = (int)SIG$1;], SIG$1)
  fi
  AC_SUBST(SIG$1)
  GASNET_FUN_END([$0])
])

dnl If PTHREAD_INCLUDE and/or PTHREAD_LIB set, check to see that pthread.h and libpthread exist,
dnl and set -I and -L to use them.  Die if set, but files don't exist
AC_DEFUN([GASNET_CHECK_OVERRIDE_PTHREADS], [
  GASNET_FUN_BEGIN([$0])
  GASNET_ENV_DEFAULT(PTHREADS_INCLUDE, )
  GASNET_ENV_DEFAULT(PTHREADS_LIB, )
  if test -n "$PTHREADS_INCLUDE" || test -n "$PTHREADS_LIB"; then
    if test -z "$PTHREADS_INCLUDE" || test -z "$PTHREADS_LIB"; then
        AC_MSG_ERROR(['Both \$PTHREADS_INCLUDE and \$PTHREADS_LIB must be set, or neither'])
    fi
    # test to see if files exist
    if test ! -f "$PTHREADS_INCLUDE/pthread.h"; then 
        AC_MSG_ERROR(["Could not find $PTHREADS_INCLUDE/pthread.h: bad \$PTHREADS_INCLUDE"])
    fi
    if test ! -f "$PTHREADS_LIB/libpthread.a" || test ! -f "$PTHREADS_LIB/libpthread.so" ; then 
        AC_MSG_ERROR(["Could not find $PTHREADS_LIB/libpthread.{a,so}: bad \$PTHREADS_LIB"])
    fi
    CFLAGS="-I$PTHREADS_INCLUDE -L$PTHREADS_LIB $CFLAGS"
  fi
  GASNET_FUN_END([$0])
])

dnl check for endianness in a cross-compiling friendly way (using an object scan)
dnl argument is optional prefix to WORDS_BIGENDIAN setting
AC_DEFUN([GASNET_BIGENDIAN], [
GASNET_FUN_BEGIN([$0($1)])
AC_REQUIRE([GASNET_PROG_PERL])
if test "$cross_compiling" = "no" ; then
  GASNET_TRY_CACHE_RUN_EXPR([whether byte ordering is bigendian $1], c_bigendian,[ ],[
    { /* Are we little or big endian?  From Harbison&Steele.  */
      union {
        long l;
        char c[[sizeof (long)]];
      } u;
      u.l = 1;
      val = (u.c[[sizeof (long) - 1]] == 1);
    }], $1[]WORDS_BIGENDIAN)
else
  AC_MSG_CHECKING(whether byte ordering is bigendian (binary probe) $1)
[
cat >conftest.$ac_ext <<EOF
short ascii_mm[] = { 0x4249, 0x4765, 0x6E44, 0x6961, 0x6E53, 0x7953, 0 };
short ascii_ii[] = { 0x694C, 0x5454, 0x656C, 0x6E45, 0x6944, 0x6E61, 0 };
void _ascii() { char* s = (char*) ascii_mm; s = (char*) ascii_ii; }
short ebcdic_ii[] = { 0x89D3, 0xE3E3, 0x8593, 0x95C5, 0x89C4, 0x9581, 0 };
short ebcdic_mm[] = { 0xC2C9, 0xC785, 0x95C4, 0x8981, 0x95E2, 0xA8E2, 0 };
void _ebcdic() { char* s = (char*) ebcdic_mm; s = (char*) ebcdic_ii; }
int main() { _ascii (); _ebcdic (); return 0; }
EOF
]
  $1[]WORDS_BIGENDIAN=""
  if test -f conftest.$ac_ext ; then
     dnl do a full link and compile, because some systems (eg X1) have an unscannable
     dnl string table in one or the other. Link first because it might clobber the .o
     if { (eval echo "$as_me:$LINENO: \"$ac_link\"") >&5
          (eval $ac_link) 2>&5
          ac_status=$?
          echo "$as_me:$LINENO: \$? = $ac_status" >&5
          (exit $ac_status); } && \
        { (eval echo "$as_me:$LINENO: \"$ac_compile\"") >&5
          (eval $ac_compile) 2>&5
          ac_status=$?
          echo "$as_me:$LINENO: \$? = $ac_status" >&5
          (exit $ac_status); } && \
        test -f conftest.o && test -f conftest$ac_exeext ; then
        # use perl here, because some greps barf on binary files (eg Solaris)
        if test `$PERL -ne 'if (m/BIGenDianSyS/) { print "yes\n"; }' conftest.o` || \
           test `$PERL -ne 'if (m/BIGenDianSyS/) { print "yes\n"; }' conftest$ac_exeext` ; then
           $1[]WORDS_BIGENDIAN=1
        fi
        if test `$PERL -ne 'if (m/LiTTleEnDian/) { print "yes\n"; }' conftest.o` || \
           test `$PERL -ne 'if (m/LiTTleEnDian/) { print "yes\n"; }' conftest$ac_exeext`; then
          if test "$[$1]WORDS_BIGENDIAN" != "1" ; then
            $1[]WORDS_BIGENDIAN=0
          fi
        fi
     fi
     dnl rm -f conftest.c conftest.o conftest$ac_exeext
  fi
  AC_MSG_RESULT($[$1]WORDS_BIGENDIAN)
fi 
if test "$[$1]WORDS_BIGENDIAN" = "1"; then
  AC_DEFINE($1[]WORDS_BIGENDIAN, 1, [whether byteorder is bigendian])
elif test "$[$1]WORDS_BIGENDIAN" = ""; then
  AC_MSG_ERROR(Inconsistent results from endian probe)
fi
GASNET_FUN_END([$0($1)])
]) 
