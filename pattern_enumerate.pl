#!/usr/bin/perl
# pattern_enumerate - enumerate distinct pattern features
#
# This makes it suitable e.g. to feed them to the MM tool.
#
# Example input:
# (border:0 s:1234) (border:8 s:4343) (border:0 s:4343)
# Example output:
# (0:0 1:0) (0:1 1:1) (0:0 1:1)
#
# Auxiliary patterns.fdict file is created, containing the mapping
# back to features, and assignment of unique number ids suitable
# e.g. for MM input.
#
# Spatial feature is treated specially, each diameter being considered
# as a separate 's.N' feature. You need to post-process the mapping
# file if you want the original names.

my %f, %fi;
my $fi = 0;

while (<>) {
	chomp;
	s/^\(//; s/\)$//;
	my @patterns = split(/\) \(/, $_);
	my @opatterns = ();
	foreach my $pat (@patterns) {
		my @feats = split(/ /, $pat);
		my @ofeats = ();
		foreach my $feat (@feats) {
			my ($fname, $fpay) = split(/:/, $feat);
			if ($fname eq 's') {
				my $d = ((hex $fpay) >> 24);
				$fname = "s.$d";
			}
			if (not defined $fi{$fname}) {
				$fi{$fname} = $fi++;
			}
			if (not defined $f{$fname}->{$fpay}) {
				$f{$fname}->{$fpay} = scalar keys %{$f{$fname}}
			}
			push @ofeats, $fi{$fname}.':'.$f{$fname}->{$fpay};
		}
		push @opatterns, join(' ', @ofeats);
	}
	print "(".join(') (', @opatterns).")\n"
}

my $pi = 0;
my $df = "patterns.fdict";
open D, ">$df" or die "$df: $!";
foreach my $fname (sort { $fi{$a} <=> $fi{$b} } keys %f) {
	foreach my $fpay (sort { $f{$fname}->{$a} <=> $f{$fname}->{$b} } keys %{$f{$fname}}) {
		print D $pi++." ".$fi{$fname}.':'.$f{$fname}->{$fpay}." $fname:$fpay\n";
	}
}
close D;
