#!/bin/sh
# pattern_bayes_gen: Generate pattern probability table from a SGF collection

(for i in "$@"; do echo $i >&2; tools/sgf2gtp.pl <$i; done) |
	./pachi -d 0 -e patternscan competition |
	perl -nle '
		BEGIN { use List::MoreUtils qw(uniq); }

		s/^= // or next;
		chomp;
		s/\) \(/),(/g;
		@a = split(/,/);

		$win = shift @a;
		$choices{$win}++;
		for (uniq @a) {
			$counts{$_}++;
		}

		END {
			for (keys %counts) {
				$p{$_} = $choices{$_} / $counts{$_};
			}
			for (sort { $counts{$a} <=> $counts{$b} } keys %p) {
				printf("%.3f %d %d %s\n", $p{$_}, $choices{$_}, $counts{$_}, $_);
			}
		}'
