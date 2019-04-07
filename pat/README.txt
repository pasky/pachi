This directory contains pattern files for Pachi. In order for Pachi
to make use of these patterns, you need at least Pachi 10.00
(or a version that contains commit 851a7).

Using patterns may result in between 50 and 150 Elo increase in strength
depending on your computer (slower computers will see more strength boost).
It will also make Pachi use about 400MiB to 600MiB more memory (which is
NOT accounted for in max_tree_size!).


Quick Start
-----------

Pick a pattern set - gogod-handikgspachi-iter is a good default choice.
Download both the patterns.prob and patterns.spat files and unpack
them in the directory where you will run Pachi. That is all.

Pachi will take noticeably longer to start up and confirm that
it has successfully loaded the patterns on its output.


Advanced Recipes
----------------

You can fine-tune Pachi's performance by passing prior=pattern=200 (or 400)
parameter to Pachi. On slower computers, this can result in significantly
stronger program, while on top machines, the default (80) may be a better fit.
We need to figure out a good way to automatically set the parameter yet.
This is something you can experiment with!

If you absolutely must, the memory usage may be reduced by:

  (i) Reducing spatial_hash_bits value in patternsp.h; each decrement
      by one will halve the memory usage, so decrement by 2 or 3 to
      get 1/4 or 1/8 memory usage by pattern structures.

  (ii) Trimming the patterns.prob file to 1/4 or 1/8 by keeping only
       the LAST lines. E.g.:

  	tail -n $((`cat patterns.prob | wc -l`/8)) patterns.prob >patterns-trimmed.prob
	mv patterns-trimmed.prob patterns.prob

On the other hand, if you have plenty of memory, raising spatial_hash_bits
value will improve Pachi's pattern-matching performance.
