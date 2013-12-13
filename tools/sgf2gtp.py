#! /usr/bin/env python

import sys
import argparse
import re

from sgflib import SGFParser

DEBUG = False

parser = argparse.ArgumentParser( formatter_class=argparse.RawDescriptionHelpFormatter,
description="""
This script converts SGF files to GTP format so that you can feed them
to Pachi, insert genmove at the right places etc. Might not work on
obscure SGF files.

When called with FILENAMES argument, it will create according output
files with .gtp extension instead of .sgf, unless --no-gtp option is
specified.

Otherwise the games are read from standard input. Note that the stdin
in this case is read in at once, so for very large collections, it is
better to run this script separately for each sgf file.

example:
    cat *.sgf | %s -g -n 5
"""%(sys.argv[0]))

parser.add_argument('FILENAMES', help='List of sgf games to process.', nargs='*', default=[])
parser.add_argument('-g', help='Automatically append genmove command for the other color.', action='store_true')
parser.add_argument('-n', help='Output at most first MOVENUM moves.', metavar='MOVENUM', type=int, default=10**10)
parser.add_argument('--stdout-only', help='Do not create the .gtp files from FILENAMES, print everything to stdout.', action='store_true')
args = vars(parser.parse_args())

class UnknownNode(Exception):
    pass

def get_atr(node, atr):
    try:
        return node.data[atr].data[0]
    except KeyError:
        return None

def get_setup(node, atr):
    try:
        return node.data[atr].data[:]
    except KeyError:
        return None

def col2num(column, board_size):
    a, o, z = map(ord, ['a', column, 'z'])
    if a <= o <= z:
        return a + board_size - o
    raise Exception( "Wrong column character: '%s'"%(column,) )

def is_pass_move(coord, board_size):
    # the pass move is represented either by [] ( = empty coord )
    # OR by [tt] (for boards <= 19 only)
    return len(coord) == 0 or ( board_size <= 19 and coord == 'tt' )

def process_gametree(gametree, fout):
    # cursor for tree traversal
    c = gametree.cursor()
    # first node is the header
    header = c.node

    handicap = get_atr(header, 'HA')
    board_size = int(get_atr(header, 'SZ') or 19)
    komi = get_atr(header, 'KM')
    player_next, player_other = "B", "W"
    setup_black = get_setup(header, 'AB')
    setup_white = get_setup(header, 'AW')

    print >>fout, "boardsize", board_size
    print >>fout, "clear_board"
    if komi:
        print >>fout, "komi", komi
    if handicap and handicap != '0':
        print >>fout, "fixed_handicap", handicap
        player_next, player_other = player_other, player_next
    if setup_black:
	for item in setup_black:
	    x, y = item
	    if x >= 'i':
		x = chr(ord(x)+1)
	    y = str(col2num(y, board_size))
	    print >>fout, "play B", x+y
    if setup_white:
	for item in setup_white:
	    x, y = item
	    if x >= 'i':
		x = chr(ord(x)+1)
	    y = str(col2num(y, board_size))
	    print >>fout, "play W", x+y

    def print_game_step(coord):
        if is_pass_move(coord, board_size):
            print >>fout, "play", player_next, "pass"
        else:
            x, y = coord
            # The reason for this incredibly weird thing is that
            # the GTP protocol excludes `i` in the coordinates
            # (do you see any purpose in this??)
            if x >= 'i':
                x = chr(ord(x)+1)
            y = str(col2num(y, board_size))
            print >>fout, "play", player_next, x+y

    movenum = 0
    # walk the game tree forward
    while 1:
        # sgf2gtp.pl ignores n = 0
        if c.atEnd or (args['n'] and movenum >= args['n']):
            break
        c.next()
        movenum += 1

        coord = get_atr(c.node, player_next)
        if coord != None:
            print_game_step(coord)
        else:
            # MAYBE white started?
            # or one of the players plays two time in a row
            player_next, player_other = player_other, player_next
            coord = get_atr(c.node, player_next)
            if coord != None:
                print_game_step(coord)
            else:
                # TODO handle weird sgf files better
                raise UnknownNode

        player_next, player_other = player_other, player_next

    if args['g']:
        print >>fout, "genmove", player_next

def process_sgf_file(fin, fout):
    sgfdata = fin.read()
    col = SGFParser(sgfdata).parse()

    for gametree in col:
        try:
            process_gametree(gametree, fout)
        except UnknownNode:
            # Try next game tree in this file
            if DEBUG:
                print >>sys.stderr, "Unknown Node"
            continue

if __name__ == "__main__":
    if not len(args['FILENAMES']):
        process_sgf_file(sys.stdin, sys.stdout)
    else:
        for in_filename in args['FILENAMES']:
            if args['stdout_only']:
                fout = sys.stdout
            else:
                if re.search('sgf$', in_filename):
                    filename_base = in_filename[:-3]
                else:
                    filename_base = in_filename
                # Save the .gtp file
                out_filename = filename_base + 'gtp'

                fout = open(out_filename, 'w')

            fin = open(in_filename, 'r')

            process_sgf_file(fin, fout)

            fin.close()
            if not args['stdout_only']:
                fout.close()

