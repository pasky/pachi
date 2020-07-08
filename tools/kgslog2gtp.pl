#!/usr/bin/perl
# usage: kgslog2gtp.pl  < kgs.log   > games.gtp
# Convert kgsGtp log file to GTP command stream
#
# The command stream contains extra game info as echo commands:
#	echo GAME_START_TIMESTAMP
#	echo starting game as COLOR against OPPONENT
#       echo final result SCORE

foreach (<STDIN>) {
    # timestamps
    #       Jul  8 07:42:12	(short timestamp format)
    if    (s/^(... .. ..:..:..) //)    {  $timestamp = $1;  }
    # Jul 08, 2020 10:37:12 AM com.gokgs.client.gtp.GtpClient main   (standard)
    if    (m/^(.*) com.gokgs.client/)  {  $timestamp = $1;  }

    # game start
    # FINE: Starting game as white against Mateman
    elsif (m/^FINE:.*as (\S+) against (\w+)/) {
	$color = substr($1, 0, 1);
	if ($timestamp ne "")  {  print "echo $timestamp\n";  }
	print "echo starting game as $1 against $2\n";
    }

    # gtp commands sent
    # FINEST: Command sent to engine: kgs-rules japanese
    # FINEST: Command queued for sending to engine: boardsize 19
    # FINEST: Queued command sent to engine: boardsize 19
    elsif (m/command sent to engine: (.*)$/i &&
	   not m/(genmove|time|name|version|final_status)/)  {  print "$1\n";  }

    # genmove answers
    # FINEST: Got successful response to command "genmove b": = D16
    # FINEST: Got successful response to command "kgs-genmove_cleanup b": = D16
    elsif (m/Got successful response to command ".*genmove.*": = (.*)$/) {
	printf("play $color %s\n", lc($1));
    }

    # game over
    # FINE: Game is over and scored (final result = B+13.5)
    elsif (m/Game is over and scored \(final result = (.*)\)/) {  print "echo final result: $1\n";  }

}
