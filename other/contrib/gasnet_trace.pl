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
my $opt_report;

my (%data, %report);
# Getting the Options
########################

GetOptions (
    'h|?|help'		=> \$opt_help,
    'sort=s'		=> \$opt_sort,
    'type'		=> \$opt_show,
    'o=s'		=> \$opt_output,
    'report=s'		=> \$opt_report
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

if (!$opt_report) {
    $opt_report="PGB";
} 

while (@ARGV) {
    parse_tracefile(pop @ARGV);
}

# Driver
########################
flatten();
sort_report();
foreach my $pgb (sort keys %report) {
    trace_output(*STDOUT, $pgb);
}
# Show program usage
########################
sub usage 
{
    print <<EOF;
Usage:	gasnet_trace [options] trace-file(s)

Options:
    -h -? -help		See this message
    -o [filename]	Output to certain file.  Default is set to STDOUT.
    -report [r1][r2]..	One or more capital letters to indicate  which 
    			reports to generate, currently support
    			P(PUT), G(GET), B(BARRIER);
    -sort [f1],[f2]...	
    			Sort the output by the given fields:
                        TOTAL_SZ, CALLS, AVG_SZ, MAX_SZ, MIN_SZ, SRC;
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
        next unless (/\[([^\]]+)\]\s+\([$opt_report]\)\s+(.*)_(.*):\D+(\d+)/); 
        
        my ($src, $pgb, $type, $sz) = ($1, $2, $3, $4);
        if (!$data{$pgb}{$src}{$type}) { # first record
            push @{$data{$pgb}{$src}{$type}}, ($sz, $sz, $sz, $sz, 1);
        } else {
            my ($max, $min, $avg, $total, $totalc) = @{$data{$pgb}{$src}{$type}};
            $max = $max > $sz ? $max : $sz;
            $min = $min < $sz ? $min : $sz;
            $total += $sz;
            $totalc += 1;
            $avg = $total / $totalc;
            @{$data{$pgb}{$src}{$type}} = ($max, $min, $avg, $total, $totalc);
	    #my @debug = @{$data{$pgb}{$src}{$type}};
        }
    }
}

# subroutine to canonicalize the msg size
# e.g -> 14336->14K, 2516582->2.4M
# args: the msg size to be canonicalized
########################
sub shorten
{
    my ($msg_sz, $type) = @_;
    if ($type =~ /GET|PUT/) {
    	if ($msg_sz < 1024) {
    	    return sprintf("%.0f B", $msg_sz);
    	} elsif ($msg_sz < 1024 * 1024) {
    	    return sprintf("%.2f K", $msg_sz / 1024.0);
    	} elsif ($msg_sz < 1024 * 1024 * 1024) {
    	    return sprintf("%.2f M", $msg_sz / (1024.0 * 1024.0));
    	} elsif ($msg_sz < 1024 * 1024 * 1024 * 1024) {
    	    return sprintf("%.2f G", $msg_sz / (1024.0 * 1024.0 * 1024.0));
    	} else {
    	    return sprintf("%.2f T", $msg_sz / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    	}
    } else {
    	if ($msg_sz < 1000) {
    	    return sprintf("%.1f us", $msg_sz);
    	} elsif ($msg_sz < 1000 * 1000) {
    	    return sprintf("%.1f ms", $msg_sz / 1000.0);
    	} elsif ($msg_sz < 1000 * 1000 * 60) {
    	    return sprintf("%.1f  s", $msg_sz / (1000.0 * 1000.0));
    	} else {
    	    return sprintf("%.1fmin", $msg_sz / (1000.0 * 1000.0 * 60.0));
    	}
    }
}

# subroutine to separate the source file name 
# and the line number
# args: the source line to be separated
#######################
sub src_line
{
    my ($line) = @_;
	
    $line =~ /(.*):(\d+)$/;    
    return ($1, $2);
}

# transfer the raw data structure into report -- a hash of arrays 
#######################
sub flatten
{
    foreach my $pgb (keys %data) {
    	foreach my $line (keys %{$data{$pgb}}) {
    	    foreach my $type (keys %{$data{$pgb}{$line}}) {
    	    	my @entry = ($line, $type, @{$data{$pgb}{$line}{$type}});
		push @{$report{$pgb}}, \@entry; 
    	    }
    	}
    }
}


# report_sorting criterion
#######################
sub criterion
{
    my @mtd = @_;
    my $result;
    my $sort_mtd = shift @mtd;
    # Breaking ties using the less important fields.
    while (!$result && $sort_mtd) {
    	if ($sort_mtd eq "CALLS") {
            $result = ${$b}[6] <=> ${$a}[6];;
    	} 
        if ($sort_mtd eq "TOTAL_SZ") {
            $result = ${$b}[5] <=> ${$a}[5];
        }
        if ($sort_mtd eq "AVG_SZ") {
            $result = ${$b}[4] <=> ${$a}[4];
        }
        if ($sort_mtd eq "MIN_SZ") {
            $result = ${$b}[3] <=> ${$a}[3];
        }
        if ($sort_mtd eq "MAX_SZ") {
            $result = ${$b}[2] <=> ${$a}[2];
        }
        if ($sort_mtd eq "SRC") {
            my ($a_src, $a_line) = src_line${$a}[0];
            my ($b_src, $b_line) = src_line${$b}[0];
            $result = ($a_src cmp $b_src) ||
                      ($a_line <=> $b_line);
        }
    	
    	$sort_mtd = shift @mtd;
    }
    return $result;
}

# sorting the report
########################
sub sort_report 
{

    my @sortmtd = split /,/, $opt_sort;
    # Checking for valid input
    foreach my $mtd (@sortmtd) {
        $mtd =~ /^(CALLS|AVG_SZ|MAX_SZ|MIN_SZ|TOTAL_SZ|SRC)$/
        or die "Could not recognize $mtd\n"; 
    }
    
    foreach my $pgb (keys %report) {
	if ($opt_sort) {
	    @{$report{$pgb}} = sort {criterion(@sortmtd)} @{$report{$pgb}};
	} else {
	    @{$report{$pgb}} = sort {criterion("TOTAL_SZ")} @{$report{$pgb}};
    	}
    }
	 
}


# subroutine to process the data structure produced by the parse_tracefile 
# subroutine and print out in a format that the caller specifies.
# args:	-filehandler -- specifying where the output should go
########################
sub trace_output 
{
    my ($handle, $pgb) = @_;
    
    # Print out 
    print "\n$pgb REPORT:\n";
    
    if ($opt_show) {
        $handle->format_name("SHOWTYPE");
    	print <<EOF
NO     SOURCE  LINE      TYPE      MSG:(min    max     avg     total)    CALLS  
==============================================================================    	
EOF
    } else {
        $handle->format_name("DEFAULT");
    	print <<EOF
NO      SOURCE LINE      MSG:(min        max       avg         total)    CALLS  
==============================================================================
EOF
    }
    
    # Setting up variables;
    my ($rank, $src_num, $source, $lnum, $type, $min, $max, $avg, $total, $calls);
    
    $rank = 1;

    foreach my $entry (@{$report{$pgb}}) { 
        ($src_num, $type, $max, $min, $avg, $total, $calls) = @{$entry};
        ($source, $lnum) = src_line($src_num);
        $max = shorten($max, $pgb);
        $min = shorten($min, $pgb);
        $avg = shorten($avg, $pgb);
        $total = shorten($total, $pgb);
        write($handle);
        $rank++;
        # Current max number of ranks is 999
        if ($rank >= 1000) {
            return;
	}
    }
    
# formats
########################

    format DEFAULT = 
@<<  @>>>>>>> @>>>>     @>>>>>>>>   @>>>>>>>>    @>>>>>>>>   @>>>>>>>>  @>>>>>
$rank, $source, $lnum, $min, $max, $avg, $total, $calls
.


    format SHOWTYPE = 
@<<  @>>>>>>> @>>>> @<<<<<<<<<  @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>
$rank, $source, $lnum, $type, $min, $max, $avg, $total, $calls
.

}

        

   
