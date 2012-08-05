#!/bin/sh
# pattern_bayes_gen: Generate pattern probability table from a SGF collection
# (or stdin GTP stream).

(if [ x"$1" = x"-" ]; then cat; else for i in "$@"; do echo $i >&2; tools/sgf2gtp.pl <$i; done; fi) |
	./pachi -d 0 -e patternscan competition,spat_split_sizes |
	perl -nle '
		BEGIN { use List::MoreUtils qw(uniq); }

		s/^= // or next;
		chomp;
		my ($winner, $witness) = (/^\[(.*)\] \[(.*)\]$/);

		sub parse { $_ = $_[0]; s/\) \(/),(/g; return split(/,/); }

		for (uniq(parse($winner))) {
			$choices{$_}++;
		}
		for (uniq(parse($witness))) {
			$counts{$_}++;
		}

		END {
			for (keys %counts) {
				$p{$_} = $choices{$_} / $counts{$_};
			}
			for (sort { $counts{$a} <=> $counts{$b} } keys %p) {
				next if ($counts{$_} < 2);
				printf("%.3f %d %d %s\n", $p{$_}, $choices{$_}, $counts{$_}, $_);
			}
		}'
