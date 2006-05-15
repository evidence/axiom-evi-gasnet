#!/usr/bin/env perl
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/gasnetrun_mpi.pl,v $
#     $Date: 2006/05/15 13:32:46 $
# $Revision: 1.47 $
# Description: GASNet MPI spawner
# Terms of use are as specified in license.txt

require 5.004;
use strict;

# NOTE: The value of $ENV{'MPIRUN_CMD'} may be set in the shell wrapper
my $spawncmd = $ENV{'MPIRUN_CMD'} || 'mpirun -np %N %P %A';
$spawncmd = stripouterquotes($spawncmd);
$spawncmd =~ s/%C/%P %A/;	# deal with common alias

# Validate the spawncmd
unless (exists($ENV{'MPIRUN_CMD_OK'}) ||
        (($spawncmd =~ m/%P/) && ($spawncmd =~ m/%A/) && ($spawncmd =~ m/%N/))) {
	die("gasnetrun: ERROR: MPIRUN_CMD='$spawncmd'\n"
          . "The environment variable MPIRUN_CMD must contain the strings '%P' and '%A'\n"
	  . "(or '%C' as an alias for '%P %A') for expansion into the program and its arguments;\n"
	  . "and '%N' for expansion into the number of processes.\n"
	  . "To disable this check, set MPIRUN_CMD_OK in your environment.\n");
}

# Globals
my $envlist = '';
my $numproc = undef;
my $numnode = undef;
my $verbose = 0;
my @verbose_opt = ("-v");
my $keep = 0;
my $dryrun = 0;
my $exename = undef;
my $uname = `uname -a`;
my $find_exe = 1;	# should we find full path of executable?
my $env_before_exe = 1; # place env cmd before exe?
my $extra_quote_argv = 0; # add extra quotes around each argument
my $group_join_argv = 0; # join all the args into one for %A?
my $force_nonempty_argv = 0; # if args are empty, still pass empty arg for %A
my $tmpdir = undef;
my $nodefile = $ENV{'GASNET_NODEFILE'} || $ENV{'PBS_NODEFILE'} ||
	($ENV{'PE_HOSTFILE'} && $ENV{'TMPDIR'} && -f "$ENV{'TMPDIR'}/machines" && "$ENV{'TMPDIR'}/machines") ||
	($ENV{'PE_HOSTFILE'} && $ENV{'TMP'} && -f "$ENV{'TMP'}/machines" && "$ENV{'TMP'}/machines");
my @tmpfiles = (defined($nodefile) && $ENV{'GASNET_RM_NODEFILE'}) ? ("$nodefile") : ();

# Define how to pass the environment vars
# 5 parameters to set: val, pre, inter, post and join
# To pass env as "-X A -Y B -Y C -Z" (a made up example)
#%envfmt = ('pre' => '-X', 'inter' => '-Y', 'post' => '-Z');
    my %envfmt = ();

# Probe for which MPI is running
    my $mpirun_cmd  = $spawncmd;
       $mpirun_cmd  =~ s/\s-.*/ -h/; # poe hangs on -help, so use -h
       $mpirun_cmd  =~ s/\s%[A-Za-z]+//g; # required for Cray MPI
    #print "probing: $mpirun_cmd\n";
    my $mpirun_help = `$mpirun_cmd 2>&1`;
    #print "probe result: $mpirun_help\n";
    my $is_lam      = ($mpirun_help =~ m|LAM/MPI|);
    my $is_ompi     = ($mpirun_help =~ m|OpenRTE|);
    my $is_mpiexec  = ($mpirun_help =~ m|mpiexec|);
    my $is_mpiexec_nt = ($mpirun_help =~ m|mpiexec| && $uname =~ m|cygwin|i );
    my $is_mpich_nt = ($mpirun_help =~ m|Unknown option| && $uname =~ m|cygwin|i );
    my $is_mpich    = ($mpirun_help =~ m|ch_p4|);
    my $is_mvich    = ($mpirun_help =~ m|MV(AP)?ICH|i);
    my $is_cray_mpi = ($mpirun_help =~ m|Psched|);
    my $is_crayt3e_mpi = ($uname =~ m|cray t3e|i );
    my $is_irix_mpi = ($mpirun_help =~ m|\[-miser\]|);
    my $is_poe      = ($mpirun_help =~ m|Parallel Operating Environment|);
    my $is_yod      = ($mpirun_help =~ m| yod |);
    my $is_bgl_mpi  = ($mpirun_help =~ m| BG/L |);
    my $is_bgl_cqsub = ($mpirun_help =~ m| cqsub .*?co/vn|);
    my $is_elan_mpi  = ($mpirun_help =~ m|ELAN|);
    my $is_jacquard = ($mpirun_help =~ m| \[-noenv\] |) && !$is_elan_mpi;
    my $envprog = $ENV{'ENVCMD'};
    if (! -x $envprog) { # SuperUX has broken "which" implementation, so avoid if possible
      $envprog = `which env`;
      chomp $envprog;
    }
    my $spawner_desc = undef;

    if ($is_lam) {
	$spawner_desc = "LAM/MPI";
	# pass env as "-x A,B,C"
	%envfmt = ( 'pre' => '-x',
		    'join' => ','
		  );
    } elsif ($is_ompi) {
	$spawner_desc = "OpenMPI";
	# pass env as "-x A -x B -x C"
	%envfmt = ( 'pre' => '-x',
		    'inter' => '-x'
		  );
    } elsif ($is_mpiexec) {
	$spawner_desc = "mpiexec/NT";
	# handles env for us
	%envfmt = ( 'noenv' => 1 );
    } elsif ($is_mpiexec) {
	$spawner_desc = "mpiexec";
	# handles env for us
	%envfmt = ( 'noenv' => 1 
		  );
	# mpiexec seems to brokenly insist on splitting argv on spaces, regardless of quoting
        # not much we can do about it...
    } elsif ($is_mpich_nt) {
	$spawner_desc = "MPICH/NT";
	# pass env as "-env A=1|B=2|C=3"
	%envfmt = ( 'pre' => '-env',
		    'join' => '|',
		    'val' => ''
		  );
	$find_exe = 0;
        $extra_quote_argv = 1;
    } elsif ($is_mvich) {
	$spawner_desc = "MVICH/MVAPICH";
	# pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	%envfmt = ( 'pre' => $envprog,
		    'val' => "'"
		  );
        $extra_quote_argv = 1;
    } elsif ($is_mpich) {
	$spawner_desc = "MPICH";
	# pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	%envfmt = ( 'pre' => $envprog,
		    'val' => "'"
		  );
    } elsif ($is_elan_mpi) {
	$spawner_desc = "Quadrics/ELAN MPI";
	# this spawner already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	# unfortunately, it also botches spaces in arguments in an unrecoverable way
    } elsif ($is_cray_mpi) {
	$spawner_desc = "Cray MPI";
	# cannot reliably use /usr/bin/env at all when running via aprun 
        # (the binary doesnt support placed execution)
	# however, the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
    } elsif ($is_crayt3e_mpi) {
	$spawner_desc = "Cray T3E MPI";
	# OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1);
    } elsif ($is_irix_mpi) {
	$spawner_desc = "IRIX MPI";
	# OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	# but spawner botches the argv quoting
        $extra_quote_argv = 1;
    } elsif ($is_poe) {
	$spawner_desc = "IBM POE";
	# the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
        $extra_quote_argv = 1;
    } elsif ($is_yod) {
	$spawner_desc = "Catamount yod";
	# the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
	# what a mess: pbsyod needs extra quoting, bare yod does not...
        #$extra_quote_argv = 1;
    } elsif ($is_bgl_mpi) {
	$spawner_desc = "IBM BG/L MPI";
	# pass as: -exp_env A -exp_env B
	%envfmt = ( 'pre' => '-exp_env',
		    'inter' => '-exp_env'
		  );
	$force_nonempty_argv = 1;
	$group_join_argv = 1;
	$env_before_exe = 0;
	@verbose_opt = ("-verbose", "2");
    } elsif ($is_bgl_cqsub) {
	$spawner_desc = "IBM BG/L cqsub";
	# pass as: -e A=val:B=val
	%envfmt = ( 'pre' => '-e',
		    'join' => ':',
		    'val' => ''
		  );
    } elsif ($is_jacquard) {
	$spawner_desc = "NERSC/Jacquard mpirun";
	if (`hostname` =~ m/jaccn/) {
	  # compute node: pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	  %envfmt = ( 'pre' => $envprog,
		      'val' => "'"
		    );
          $extra_quote_argv = 1;
	} else {
	  # front-end node: pass env as [/usr/bin/env '"A=1"' '"B=2"' '"C=3"'] to allow for extra shell
	  %envfmt = ( 'pre' => $envprog,
		      'lquote' => "'\"",
		      'rquote' => "\"'"
		    );
          $extra_quote_argv = 2;
	}
    } else {
	$spawner_desc = "unknown program (using generic MPI spawner)";
	# the OS already propagates the environment for us automatically
	# pass env as "/usr/bin/env A=1 B=2 C=3"
	# Our nearly universal default
	%envfmt = ( 'pre' => $envprog,
		    'val' => ''
		  );
    }

sub usage
{
    print (@_) if (@_);

    print "usage: gasnetrun -n <n> [options] [--] prog [program args]\n";
    print "    options:\n";
    print "      -n <n>                number of processes to run\n";
    print "      -N <n>                number of nodes to run on (not suppored on all mpiruns)\n";
    print "      -E <VAR1[,VAR2...]>   list of environment vars to propagate\n";
    print "      -v                    be verbose about what is happening\n";
    print "      -t                    test only, don't execute anything (implies -v)\n";
    print "      -k                    keep any temporary files created (implies -v)\n";
    print "      --                    ends option parsing\n";
    exit 1;
}

sub stripouterquotes {
    my ($val) = @_;
    while ( $val =~ s/['"](.*?)['"]/$1/ ) { }
    return $val;
}

# "Multiply" array(s) for mapping procs to nodes
sub expand {
  my $ppn = int($numproc / $numnode);
  my $full = $numproc - $numnode * $ppn;  # nodes carrying ($ppn + 1) procs
  my $part = $numnode - $full;       # nodes carrying $ppn procs
                                                                                                              
  while (my $arr_ref = shift @_) {
    my @tmp = ();
    for (my $i = 0; $i < $full; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j <= $ppn; ++$j) { push @tmp, $elem; }
    }
    for (my $i = 0; $i < $part; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j < $ppn; ++$j) { push @tmp, $elem; }
    }
    @$arr_ref = @tmp;
  }
}

# We need to parse our command-line arguments
    while (@ARGV > 0) {
	$_ = $ARGV[0];

	if ($_ eq '--') {
	    shift;
	    last;
	} elsif ($_ eq '-n' || $_ eq '-np') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numproc = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numproc >= 1;
	} elsif ($_ =~ /^(-np?)([0-9]+)$/) {
	    $numproc = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numproc >= 1;
	} elsif ($_ eq '-N') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numnode = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numnode >= 1;
	} elsif ($_ =~ /^(-N)([0-9]+)$/) {
	    $numnode = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numnode >= 1;
	} elsif ($_ eq '-E') {
	    shift;
	    usage ("-E option given without an argument\n") unless @ARGV >= 1;
	    $envlist = $ARGV[0];
	} elsif ($_ eq '-v') {
	    $verbose = 1;
	} elsif ($_ eq '-t') {
	    $dryrun = 1;
	    $verbose = 1;
	} elsif ($_ eq '-k') {
	    $keep = 1;
	    $verbose = 1;
	} elsif (m/^-/) {
	    usage ("unrecognized option '$_'\n");
	} else {
	    last;
	}
	shift;
    }

    print "gasnetrun: identified MPI spawner as: $spawner_desc\n" if ($verbose);

# Validate -n as needed
    if (!defined($numproc) && $spawncmd =~ /%N/) {
	usage "Required option -n was not given\n";
    }

# Validate -N as needed
    if (defined($numnode) && !$is_lam) {
	warn "WARNING: Don't know how to control process->node layout with your mpirun\n";
	warn "WARNING: PROCESS LAYOUT MIGHT NOT MATCH YOUR REQUEST\n";
    }

# Find the program
    my $exebase = shift or usage "No program specified\n";
    if ($find_exe) {
        if ($exebase =~ m|^/|) {
	    # full path, don't do anything to it
	    $exename = $exebase;
        } elsif ($exebase =~ m|/| || -x $exebase) {
	    # has directory components or exists in cwd
	    my $cwd = `pwd`;
	    chomp $cwd;
	    $exename = "$cwd/$exebase";
        } else {
	    # search PATH
	    foreach (split(':', $ENV{PATH})) {
	        my $tmp = "$_/$exebase";
	        if (-x $tmp) {
		    $exename = $tmp;
		    last;
	        }
	    }
        }
	if ($uname =~ m|cygwin|i) { # convert cygwin paths to windows paths for non-cygwin spawners
	  $exename = `cygpath -a -m $exename`;
	  chomp($exename);
	}
        die("gasnetrun: unable to locate program '$exebase'\n")
		    unless (defined($exename) && -x $exename);
        print("gasnetrun: located executable '$exename'\n") if ($verbose);
    } else {
        $exename = $exebase;
    }

# We need to gather a list of important environment variables
    # Form a list of the vars given by -E, plus any GASNET_* vars
    $ENV{"GASNET_VERBOSEENV"} = "1" if ($verbose);
    my @envvars = ((grep {+exists($ENV{$_})} split(',', $envlist)),
		   (grep {+m/^GASNET_/} keys(%ENV)));

# Build up the environment-passing arguments in several steps
    my @envargs = @envvars;
    if (@envvars) {
        # pair the variables with their values if desired
        if (defined $envfmt{val}) {
	    my $q = $envfmt{val};
	    @envargs = map { "$_=$q$ENV{$_}$q" } @envargs;
        } elsif (defined $envfmt{lquote} || defined $envfmt{rquote}) {
	    my $lq = $envfmt{lquote};
	    my $rq = $envfmt{rquote};
	    @envargs = map { "$_=$lq$ENV{$_}$rq" } @envargs;
        }
        # join them into a single argument if desired
        if (defined $envfmt{join}) {
	    @envargs = join($envfmt{join}, @envargs);
        }
        # introduce 'inter' arg between variable (no effect if already joined)
        if (defined $envfmt{inter}) {
	    @envargs = map { ($_, $envfmt{inter}) } @envargs;
	    pop @envargs;
        }
        # tack on 'pre' and 'post' args
        if (defined $envfmt{pre}) {
	    unshift @envargs, $envfmt{pre};
        }
        if (defined $envfmt{post}) {
	    push @envargs, $envfmt{post};
        }
	if (defined $envfmt{noenv}) {
	    @envargs = ();
	}
    }
    print "envargs: " . (join " ", @envargs) . "\n" if ($verbose);

    # Special case for the mpich spawner
    if ($is_mpich && !$is_mpich_nt && !$is_mvich) {
        # General approach: create a wrapper script for the rsh/ssh command invoked by MPICH
        # that glues on the correct environment variables in a way that won't disturb MPICH
        $tmpdir = "gasnetrun_mpi-temp-$$";
        mkdir ($tmpdir, 0777) or die "gasnetrun: cannot create \'$tmpdir\'";
	my @spawners = ('ssh', 'rsh'); # default is to create ssh and rsh capture scripts
                                       # always create them because MPICH-GM device overrides RSHCOMMAND
        my $realprog = undef;
        my $realprog_args = undef;
        # If we have a direct path to the MPICH spawn script, rewrite it and replace RSHCOMMAND
        # for the most robust spawner capture (because RSHCOMMAND is sometimes an absolute path)
        if ($spawncmd =~ /^\s*(\S+)/ && -x "$1" && `grep 'RSHCOMMAND=' "$1" 2> /dev/null` ne "") {
          my $mpirun_script = stripouterquotes($1);
          my $tmprun = "$tmpdir/mpirun-tmp";
          my $tmprsh = 'mpirun-rsh';
          open (MPIRUN, $mpirun_script) or die "gasnetrun: can't open '$mpirun_script' for reading\n";
          open (TMPRUN, ">$tmprun") or die "gasnetrun: can't open '$tmprun' for writing\n";
          print "gasnetrun: cloning '$mpirun_script' to '$tmprun'\n" if ($verbose);
          while (<MPIRUN>) {
            my $line = $_;
            if ($line =~ /^\s*RSHCOMMAND=(.+)$/) {
              $realprog = $1;
              $line =~ s/$realprog/"$tmprsh"/; 
              $realprog = stripouterquotes($realprog);
              $realprog =~ s/^(\S+)\s*(.*)$/$1/;
              $realprog_args = $2;
              $realprog = stripouterquotes($realprog);
            }
            print TMPRUN "$line";
          }
          close (MPIRUN);
          close (TMPRUN);
	  chmod 0700, $tmprun or die "gasnetrun: cannot \'chmod 0700, $tmprun\'";
	  unshift @tmpfiles, "$tmprun";
	  if (!($realprog =~ /^\//)) { # RSHCOMMAND is a relative path - get absolute
             chomp($realprog = `which "$realprog" 2> /dev/null` || $realprog);
          }
          if (! -x "$realprog") {
	    print "gasnetrun: warning: cannot find MPICH underlying spawner '$realprog'\n" if ($verbose);
            $realprog = `which "ssh" 2> /dev/null`; $realprog_args = undef;
          }
	  unshift @spawners, $tmprsh;
          $spawncmd =~ s#$mpirun_script#$tmprun#;
        } 
	my $args = join(' ',map { "\"\'$_\'\"" } @envargs);
	(my $degooped_exename = $exename) =~ s/#/\\#/g;
	(my $degooped_args = join(' ',map { "\'$_\'" } @envargs)) =~ s/#/\\#/g;
	foreach my $spawner (@spawners) {
          unless (defined $realprog) { chomp($realprog = `which "$spawner" 2> /dev/null`); }
	  if (! -x "$realprog") { # Can't find that spawner - Assume we're not using it
            print "gasnetrun: warning: cannot find \'$spawner\'\n" if ($verbose);
	    next;
  	  }

	  my $tmpfile = "$tmpdir/$spawner";
	  unshift @tmpfiles, "$tmpfile";
          print "gasnetrun: building '$tmpfile' to wrap '$realprog'\n" if ($verbose);
	  open (TMPSPAWN, ">$tmpfile") or die "gasnetrun: cannot open $tmpfile";
	  print TMPSPAWN <<EOF;
#!/bin/sh

for arg in "\$@" ; do 
  #echo \$arg
  shift
  if test "\$arg" = "$exename" ; then 
    # prog name appears alone as an argument - prepend our env command
    set -- "\$@" $args "\'\$arg\'"
  elif test "\`echo \"\$arg\" | grep \"$exename\" ; exit 0\`"; then
    # prog name appears embedded in a larger quoted argument (eg mpich_gm/rsh)
    # keep it as one large arg and insert our env call
    newarg=`echo \"\$arg\" | sed \"s#$degooped_exename#$degooped_args \'$degooped_exename\'#\"`
    set -- "\$@" "\$newarg"
  else
    set -- "\$@" "\$arg"
  fi
done

if test "$verbose" != "0" ; then
  echo \$0 executing command:
  echo '$realprog' $realprog_args "\$@"
fi
  exec '$realprog' $realprog_args "\$@"
EOF
	  close(TMPSPAWN);
	  chmod 0700, $tmpfile or die "gasnetrun: cannot \'chmod 0700, $tmpfile\'";
          $realprog = undef;
          $realprog_args = undef;
   	}	
	$ENV{PATH} = "$tmpdir:$ENV{PATH}";
	@envargs = ();
     }
    
# Exec it
    my $cwd = `pwd`;
    chomp $cwd;
    my @spawncmd = map {  if ($_ eq '%N') {
			      if ($is_lam && $numnode) {
				  my @tmp = (0..($numnode-1));
				  expand \@tmp;
				  ($numproc, 'n' . join(',', @tmp));
			      } else {
				  $numproc;
			      }
			  } elsif ($_ eq '%H') {
                              $nodefile or die "gasnetrun: %H appears in MPIRUN_CMD, but GASNET_NODEFILE is not set in the environment\n";
			  } elsif ($_ eq '%P') {
			      ( $env_before_exe ? (@envargs, $exename) : ($exename, @envargs) );
		              #my @tmp = @envargs;
			      #if ($env_before_exe) { push(@tmp, $exename); }
 			      #else { unshift(@tmp, $exename); }
			      #@tmp;
			  } elsif ($_ eq '%D') {
                              $cwd;
                          } elsif ($_ eq '%A') {
			      my @argv = ( $extra_quote_argv == 1 ? (map { "'$_'" } @ARGV)
					 : $extra_quote_argv == 2 ? (map { "'\"$_\"'" } @ARGV)
					 : (@ARGV) );
			      ($force_nonempty_argv && !@argv ? ("") : 
                                ($group_join_argv ? join(' ', @argv) : @argv) );
                          } elsif ($_ eq '%V') {
			      $verbose?@verbose_opt:();
			  } else {
                              $_;
                          }
			} split(" ", $spawncmd);
    if ($verbose) { 
      my @quotedcmd = map {
           if (m/[ ]/ || m/^$/) {
	     "'$_'"; # quote tricky args for readability
           } else {
	     $_;
           } 
         } @spawncmd;
      print("gasnetrun: running: ", join(' ', @quotedcmd), "\n");
    }

    if ($dryrun) {
	# Do nothing
    } elsif (@tmpfiles || defined($tmpdir)) {
	system(@spawncmd);
	if (!$keep) {
          foreach (@tmpfiles) {
	    print("gasnetrun: unlinking ", join(' ', @tmpfiles), "\n") if ($verbose);
	    unlink "$_" or die "gasnetrun: failed to unlink \'$_\'";
	  }
	  if (defined($tmpdir)) {
	    rmdir $tmpdir or die "gasnetrun: failed to rmdir \'$tmpdir\'";
	  }
 	}
    } else {
	exec(@spawncmd);
	die "gasnetrun: exec failed: $!\n";
    }
    exit(0);
__END__
