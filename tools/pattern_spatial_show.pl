#!/usr/bin/perl
# pattern_spatial_show: Show given spatial dictionary entry on board

my $f = "patterns.spat";
my $id = $ARGV[0];

my @ofs = ();

open D, "$f" or die "$f: $!";
while (<D>) {
	chomp;
	if (s/^# Point order: d=(\d+) //) {
		my $d = $1; next if ($d < 1);
		# XXX: Distances must be in proper order!
		s/ *$//;
		push @ofs, map { [ split(/,/, $_) ] } split(/ /, $_);
		# print "#### $_\n";
		# print "[$d] ($#ofs) " . join(' ', map { $_->[0].",".$_->[1] } @ofs) . "\n";
		next;
	}
	/^#/ and next;
	my ($lid, $d, $pat, @hashes) = split (/\s+/, $_);
	if ($id == $lid) {
		print "$d $pat\n";
		my @b;
		my @pc = split (//, $pat);
		for my $i (0 .. $#pc) {
			# print "$i: $pc[$i] -> $ofs[$i][1],$ofs[$i][0]\n";
			$b[$d + $ofs[$i][1]][$d + $ofs[$i][0]] = $pc[$i];
		}
		$b[$d][$d] = '_';
		for my $y (0 .. 2 * $d) {
			for my $x (0 .. 2 * $d) {
				print $b[2 * $d - $y][$x] || ' ';
				print ' ';
			}
			print "\n";
		}
		last;
	}
}
