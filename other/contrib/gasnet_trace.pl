#!/usr/bin/env perl

use strict;
use FileHandle;
use Getopt::Long;
    
# Global Variables
########################

my $opt_sort;
my $opt_output;
my $opt_show;
my $opt_help;

my %src;     # Each package sent
my %func;    # If it is put or get
my %type;    # What kind of put / get
my @data = (\%src, \%func, \%type);

# Getting the Options
########################

GetOptions (
    'h|?|help'		=> \$opt_help,
    'sort=s'		=> \$opt_sort,
    'type'		=> \$opt_show,
    'o=s'		=> \$opt_output
);

# The main routine
########################
usage() if $opt_help;

if (!@ARGV) {
    usage (-1);
}

if ($opt_output) {
    open(STDOUT, ">$opt_output") or die "Could not write to $opt_output: $!\n";
}

while (@ARGV) {
    parse_tracefile(pop @ARGV);
}

trace_output(*STDOUT); 

# Show program usage
########################
sub usage 
{
    print <<EOF;
Usage:	gasnet_trace [options] trace-file(s)

Options:
    -h -? -help		See this message
    -o [filename]	Output to certain file.  
                        Default is set to STDOUT.
    -sort [f1],[f2]...	
    			Sort the output by the given fields:
                        TOTAL_SZ, CALLS, AVG_SZ, MAX_SZ, MIN_SZ;
                        Default is set to TOTAL_SZ;
    -type		Show the types of get and put in the output.
EOF
    exit(-1);
} 	


    
# subroutine to read the tracefile and dump the useful information into a 
# data-structure, namely an array of hashes and return the array.
# args : the filename to be read.
########################
sub parse_tracefile 
{
    
    open (TRACEFILE, $_[0]) or die "Could not open $_[0]: $!\n";
    
    while (<TRACEFILE>) {
        next unless /\[([^\]]+)\]\s+\([GP]\)\s+(.*?):\D+(\d+)/;
        push @{$src{$1}}, $3;
        if (!$func{$1}) {
        $func{$1} = substr($2, 0, 3);
        $type{$1} = substr($2, 4);
        }
    }
}

# subroutine to canonicalize the msg size
# e.g -> 14336->14K, 2516582->2.4M
# args: the msg size to be canonicalized
########################
sub shorten
{
    my ($msg_sz) = @_;
    if ($msg_sz < 1024) {
    	return "$msg_sz" ."B";
    } elsif ($msg_sz < 1024 * 1024) {
    	return sprintf("%.2fK", $msg_sz / 1024.0);
    } elsif ($msg_sz < 1024 * 1024 * 1024) {
    	return sprintf("%.2fM", $msg_sz / (1024.0 * 1024.0));
    } elsif ($msg_sz < 1024 * 1024 * 1024) {
    	return sprintf("%.2fG", $msg_sz / (1024.0 * 1024.0 * 1024.0));
    } else {
    	return sprintf("%.2fT", $msg_sz / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    }
}


# subroutine to process the data structure produced by the parse_tracefile 
# subroutine and print out in a format that the caller specifies.
# args:	-filehandler -- specifying where the output should go
########################
sub trace_output 
{
    my $handle = $_[0];
    
    my @lines = keys %src;
    my %totalsz;
    my %minsz;
    my %maxsz;
    my %avgsz;
   
    foreach my $line (@lines) {
	my @packages = @{$src{$line}};
	my $currentsz;
	for (my $i=0 ; $i < scalar(@packages); $i++) {
	    $currentsz = $packages[$i];	
	    $totalsz{$line} += $currentsz;
            if ($maxsz{$line} < $currentsz) {
                $maxsz{$line} = $currentsz;
            }
            if ((!$minsz{$line}) or ($minsz{$line} > $currentsz)) {
                $minsz{$line} = $currentsz;
            }
	}
	$avgsz{$line} = $totalsz{$line} / (scalar(@packages));
    }
    
    
    
    local *method = 
    sub {
    	my @mtd = @_;
    
    	my $result;
    	my $sort_mtd = shift @mtd;
        # Breaking ties using the less important fields.
        while (!$result && $sort_mtd) {
            SWITCH: {
                if ($sort_mtd eq "CALLS") {
                    $result = scalar(@{$src{$b}}) <=> scalar(@{$src{$a}});
                } 
                if ($sort_mtd eq "AVG_SZ") {
                    $result = $avgsz{$b} <=> $avgsz{$a};
                }
                if ($sort_mtd eq "MAX_SZ") {
                    $result = $maxsz{$b} <=> $maxsz{$a};
                }
                if ($sort_mtd eq "MIN_SZ") {
                    $result = $minsz{$b} <=> $minsz{$a};
                }
                if ($sort_mtd eq "TOTAL_SZ") {
                    $result = $totalsz{$b} <=> $totalsz{$a};
                }
            }
           $sort_mtd = shift @mtd;
        }
        return $result;
    }; 
    
    my @sortmtd = split /,/, $opt_sort;
    my @sorted;
    # Checking for valid input
    foreach my $mtd (@sortmtd) {
        $mtd =~ /^(CALLS|AVG_SZ|MAX_SZ|MIN_SZ|TOTAL_SZ)$/
        or die "Could not recognize $mtd\n" 
    }
    if ($opt_sort) {     
        @sorted = sort {method(@sortmtd)} @lines;
    } else {
        @sorted = sort {method("TOTAL_SZ")} @lines;
    }
    
    
    # Print out 
    my $rank = 1;
    my ($entry, $line, $source, $lnum);
    my ($type, $pg, $min, $max, $avg, $total, $calls);
    
    if ($opt_show) {
        $handle->format_name("SHOWTYPE");
    } else {
        $handle->format_name("DEFAULT");
    }
    foreach $line (@sorted) {
	$line =~ /(.*):(\d+)$/;
	$source = $1;
	$lnum = $2;
	
	$pg = $func{$line};
	$type = $type{$line}; 
	$min = shorten($minsz{$line});
	$max = shorten($maxsz{$line});
	$avg = shorten($avgsz{$line});
	$total = shorten($totalsz{$line});
	$calls = scalar(@{$src{$line}});
	
	write($handle);
	$rank++;
	# Current max number of ranks is 999
	if ($rank >= 1000) {
	    return;
	}
    }   
    
# formats
########################

    format DEFAULT_TOP =  
NO      SOURCE LINE  PG  MSG:(min        max       avg         total)   CALLS  
=============================================================================
.
    
    format DEFAULT = 
@<<  @>>>>>>> @>>>> @>> @>>>>>>>   @>>>>>>>     @>>>>>>>     @>>>>>>>  @>>>>>
$rank, $source, $lnum, $pg, $min, $max, $avg, $total, $calls
.


    format SHOWTYPE_TOP =  
NO     SOURCE  LINE PG   TYPE      MSG:(min    max     avg     total)   CALLS  
=============================================================================
.
    
    format SHOWTYPE = 
@<<  @>>>>>>> @>>>> @>> @<<<<<<<<  @>>>>>>> @>>>>>>> @>>>>>>> @>>>>>>> @>>>>>
$rank, $source, $lnum, $pg, $type, $min, $max, $avg, $total, $calls
.

}

        

   
