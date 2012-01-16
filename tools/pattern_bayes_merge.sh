#!/bin/sh
# pattern_bayes_merge: Merge pattern probability tables; pass them
# as arguments.

cat "$@" | perl -nle '
	chomp;
	my ($p, $ch, $co, $pa) = /^(.*?) (\d+) (\d+) (.*)$/;
	$pa or die "parse error: $_";
	$choices{$pa} += $ch;
	$counts{$pa} += $co;

	END {
		for (keys %counts) {
			$p{$_} = $choices{$_} / $counts{$_};
		}
		for (sort { $counts{$a} <=> $counts{$b} } keys %p) {
			next if ($counts{$_} < 2);
			printf("%.3f %d %d %s\n", $p{$_}, $choices{$_}, $counts{$_}, $_);
		}
	}
	'
