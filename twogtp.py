#! /usr/bin/env python

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
# This program is distributed with GNU Go, a Go program.            #
#                                                                   #
# Write gnugo@gnu.org or see http://www.gnu.org/software/gnugo/     #
# for more information.                                             #
#                                                                   #
# Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 and 2007 #
# by the Free Software Foundation.                                  #
#                                                                   #
# This program is free software; you can redistribute it and/or     #
# modify it under the terms of the GNU General Public License       #
# as published by the Free Software Foundation - version 3,         #
# or (at your option) any later version.                            #
#                                                                   #
# This program is distributed in the hope that it will be           #
# useful, but WITHOUT ANY WARRANTY; without even the implied        #
# warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR           #
# PURPOSE.  See the GNU General Public License in file COPYING      #
# for more details.                                                 #
#                                                                   #
# You should have received a copy of the GNU General Public         #
# License along with this program; if not, write to the Free        #
# Software Foundation, Inc., 51 Franklin Street, Fifth Floor,       #
# Boston, MA 02111, USA.                                            #
# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #

from getopt import *
import popen2
import sys
import string
import re


debug = 0


def coords_to_sgf(size, board_coords):
    global debug
    
    board_coords = string.lower(board_coords)
    if board_coords == "pass":
        return ""
    if debug:
        print "Coords: <" + board_coords + ">"
    letter = board_coords[0]
    digits = board_coords[1:]
    if letter > "i":
        sgffirst = chr(ord(letter) - 1)
    else:
        sgffirst = letter
    sgfsecond = chr(ord("a") + int(size) - int(digits))
    return sgffirst + sgfsecond



class GTP_connection:

    #
    # Class members:
    #   outfile         File to write to
    #   infile          File to read from

    def __init__(self, command):
        try:
            infile, outfile = popen2.popen2(command)
        except:
            print "popen2 failed"
            sys.exit(1)
        self.infile  = infile
        self.outfile = outfile
        
    def exec_cmd(self, cmd):
        global debug
        
        if debug:
            sys.stderr.write("GTP command: " + cmd + "\n")
        self.outfile.write(cmd + "\n\n")
        self.outfile.flush()
        result = ""
        line = self.infile.readline()
        while line != "\n":
            result = result + line
            line = self.infile.readline()
        if debug:
            sys.stderr.write("Reply: " + line + "\n")

        # Remove trailing newline from the result
        if result[-1] == "\n":
            result = result[:-1]

        if len(result) == 0:
            return "ERROR: len = 0"
        if (result[0] == "?"):
            return "ERROR: GTP Command failed: " + result[2:]
        if (result[0] == "="):
            return result[2:]
        return "ERROR: Unrecognized answer: " + result
        

class GTP_player:

    # Class members:
    #    connection     GTP_connection

    def __init__(self, command):
        self.connection = GTP_connection(command)
        protocol_version = self.connection.exec_cmd("protocol_version")
        if protocol_version[:5] != "ERROR":
            self.protocol_version = protocol_version
        else:
            self.protocol_version = "1"

    def is_known_command(self, command):
        return self.connection.exec_cmd("known_command " + command) == "true"

    def genmove(self, color):
        if color[0] in ["b", "B"]:
            command = "black"
        elif color[0] in ["w", "W"]:
            command = "white"
        if self.protocol_version == "1":
            command = "genmove_" + command
        else:
            command = "genmove " + command

        return self.connection.exec_cmd(command)

    def black(self, move):
        if self.protocol_version == "1":
            self.connection.exec_cmd("black " + move)
        else:
            self.connection.exec_cmd("play black " + move)

    def white(self, move):
        if self.protocol_version == "1":
            self.connection.exec_cmd("white " + move)
        else:
            self.connection.exec_cmd("play white " + move)

    def komi(self, komi):
        self.connection.exec_cmd("komi " + komi)

    def boardsize(self, size):
        self.connection.exec_cmd("boardsize " + size)
        if self.protocol_version != "1":
            self.connection.exec_cmd("clear_board")

    def handicap(self, handicap, handicap_type):
        if handicap_type == "fixed":
            result = self.connection.exec_cmd("fixed_handicap %d" % (handicap))
        else:
            result = self.connection.exec_cmd("place_free_handicap %d"
                                              % (handicap))

        return string.split(result, " ")

    def loadsgf(self, endgamefile, move_number):
        self.connection.exec_cmd(string.join(["loadsgf", endgamefile,
                                 str(move_number)]))

    def list_stones(self, color):
        return string.split(self.connection.exec_cmd("list_stones " + color), " ")

    def quit(self):
        return self.connection.exec_cmd("quit")
    
    def showboard(self):
        board = self.connection.exec_cmd("showboard")
        if board and (board[0] == "\n"):
            board = board[1:]
        return board

    def get_random_seed(self):
        result = self.connection.exec_cmd("get_random_seed")
        if result[:5] == "ERROR":
            return "unknown"
        return result

    def set_random_seed(self, seed):
        self.connection.exec_cmd("set_random_seed " + seed)

    def get_program_name(self):
        return self.connection.exec_cmd("name") + " " + \
               self.connection.exec_cmd("version")

    def final_score(self):
        return self.connection.exec_cmd("final_score")

    def score(self):
        return self.final_score(self)

    def cputime(self):
        if (self.is_known_command("cputime")):
            return self.connection.exec_cmd("cputime")
        else:
            return "0"


class GTP_game:

    # Class members:
    #    whiteplayer     GTP_player
    #    blackplayer     GTP_player
    #    size            int
    #    komi            float
    #    handicap        int
    #    handicap_type   string
    #    handicap_stones int
    #    moves           list of string
    #    resultw
    #    resultb

    def __init__(self, whitecommand, blackcommand, size, komi, handicap,
                 handicap_type, endgamefile):
        self.whiteplayer = GTP_player(whitecommand)
        self.blackplayer = GTP_player(blackcommand)
        self.size = size
        self.komi = komi
        self.handicap = handicap
        self.handicap_type = handicap_type
        self.endgamefile = endgamefile
        self.sgffilestart = ""
        if endgamefile != "":
            self.init_endgame_contest_game()
        else:
            self.sgffilestart = ""

    def init_endgame_contest_game(self):
        infile = open(self.endgamefile)
        if not infile:
            print "Couldn't read " + self.endgamefile
            sys.exit(2)
        sgflines = infile.readlines()
        infile.close
        size = re.compile("SZ\[[0-9]+\]")
        move = re.compile(";[BW]\[[a-z]{0,2}\]")
        sgf_start = []
        for line in sgflines:
            match = size.search(line)
            if match:
                self.size = match.group()[3:-1]
            match = move.search(line)
            while match:
                sgf_start.append("A" + match.group()[1:])
                line = line[match.end():]
                match = move.search(line)
        self.endgame_start = len(sgf_start) - endgame_start_at
        self.sgffilestart = ";" + string.join(
                                    sgf_start[:self.endgame_start-1], "") + "\n"
        if self.endgame_start % 2 == 0:
            self.first_to_play = "W"
        else:
            self.first_to_play = "B"

    def get_position_from_engine(self, engine):
        black_stones = engine.list_stones("black")
        white_stones = engine.list_stones("white")
        self.sgffilestart = ";"
        if len(black_stones) > 0:
            self.sgffilestart += "AB"
            for stone in black_stones:
                self.sgffilestart += "[%s]" % coords_to_sgf(self.size, stone)
            self.sgffilestart += "\n"
        if len(white_stones) > 0:
            self.sgffilestart += "AW"
            for stone in white_stones:
                self.sgffilestart += "[%s]" % coords_to_sgf(self.size, stone)
            self.sgffilestart += "\n"

    def writesgf(self, sgffilename):
        "Write the game to an SGF file after a game"

        size = self.size
        outfile = open(sgffilename, "w")
        if not outfile:
            print "Couldn't create " + sgffilename
            return
        black_name = self.blackplayer.get_program_name()
        white_name = self.whiteplayer.get_program_name()
        black_seed = self.blackplayer.get_random_seed()
        white_seed = self.whiteplayer.get_random_seed()
        handicap = self.handicap
        komi     = self.komi
        result   = self.resultw

        outfile.write("(;GM[1]FF[4]RU[Japanese]SZ[%s]HA[%s]KM[%s]RE[%s]\n" %
                      (size, handicap, komi, result))
        outfile.write("PW[%s (random seed %s)]PB[%s (random seed %s)]\n" %
                      (white_name, white_seed, black_name, black_seed))
        outfile.write(self.sgffilestart)

        if handicap > 1:
            outfile.write("AB");
            for stone in self.handicap_stones:
                outfile.write("[%s]" %(coords_to_sgf(size, stone)))
            outfile.write("PL[W]\n")

        to_play = self.first_to_play

        for move in self.moves:
            sgfmove = coords_to_sgf(size, move)
            outfile.write(";%s[%s]\n" % (to_play, sgfmove))
            if to_play == "B":
                to_play = "W"
            else:
                to_play = "B"
        outfile.write(")\n")
        outfile.close

    def set_handicap(self, handicap):
        self.handicap = handicap

    def swap_players(self):
        temp = self.whiteplayer
        self.whiteplayer = self.blackplayer
        self.blackplayer = temp

    def play(self, sgffile):
        "Play a game"
        global verbose

        if verbose >= 1:
            print "Setting boardsize and komi for black\n"
        self.blackplayer.boardsize(self.size)
        self.blackplayer.komi(self.komi)

        if verbose >= 1:
            print "Setting boardsize and komi for white\n"
        self.whiteplayer.boardsize(self.size)
        self.whiteplayer.komi(self.komi)

        self.handicap_stones = []

        if self.endgamefile == "":
            if self.handicap < 2:
                self.first_to_play = "B"
            else:
                self.handicap_stones = self.blackplayer.handicap(self.handicap, self.handicap_type)
                for stone in self.handicap_stones:
                    self.whiteplayer.black(stone)
                self.first_to_play = "W"
        else:
            self.blackplayer.loadsgf(self.endgamefile, self.endgame_start)
            self.blackplayer.set_random_seed("0")
            self.whiteplayer.loadsgf(self.endgamefile, self.endgame_start)
            self.whiteplayer.set_random_seed("0")
            if self.blackplayer.is_known_command("list_stones"):
                self.get_position_from_engine(self.blackplayer)
            elif self.whiteplayer.is_known_command("list_stones"):
                self.get_position_from_engine(self.whiteplayer)

        to_play = self.first_to_play

        self.moves = []
        passes = 0
        won_by_resignation = ""
        while passes < 2:
            if to_play == "B":
                move = self.blackplayer.genmove("black")

                if move[:5] == "ERROR":
                    # FIXME: write_sgf
                    sys.exit(1)

                if move[:6] == "resign":
                    if verbose >= 1:
                        print "Black resigns"
                    won_by_resignation = "W+Resign"
                    break
                else:
                    self.moves.append(move)
                    if string.lower(move[:4]) == "pass":
                        passes = passes + 1
                        if verbose >= 1:
                            print "Black passes"
                    else:
                        passes = 0
                        self.whiteplayer.black(move)
                        if verbose >= 1:
                            print "Black plays " + move
                to_play = "W"
            else:
                move = self.whiteplayer.genmove("white")
                if move[:5] == "ERROR":
                    # FIXME: write_sgf
                    sys.exit(1)

                if move[:6] == "resign":
                    if verbose >= 1:
                        print "White resigns"
                    won_by_resignation = "B+Resign"
                    break
                else:
                    self.moves.append(move)
                    if string.lower(move[:4]) == "pass":
                        passes = passes + 1
                        if verbose >= 1:
                            print "White passes"
                    else:
                        passes = 0
                        self.blackplayer.white(move)
                        if verbose >= 1:
                            print "White plays " + move
                to_play = "B"

            if verbose >= 2:
                print self.whiteplayer.showboard() + "\n"

        if won_by_resignation == "":
            self.resultw = self.whiteplayer.final_score()
            self.resultb = self.blackplayer.final_score()
        else:
            self.resultw = won_by_resignation;
            self.resultb = won_by_resignation;
    #    if self.resultb == self.resultw:
    #        print "Result: ", self.resultw
    #    else:
    #        print "Result according to W: ", self.resultw
    #        print "Result according to B: ", self.resultb
        # FIXME:   $self->writesgf($sgffile) if defined $sgffile;
        if sgffile != "":
            self.writesgf(sgffile)

    def result(self):
        return (self.resultw, self.resultb)

    def cputime(self):
        cputime = {}
        cputime["white"] = self.whiteplayer.cputime()
        cputime["black"] = self.blackplayer.cputime()
        return cputime
    
    def quit(self):
        self.blackplayer.quit()
        self.whiteplayer.quit()


class GTP_match:

    # Class members:
    #    black
    #    white
    #    size
    #    komi
    #    handicap
    #    handicap_type

    def __init__(self, whitecommand, blackcommand, size, komi, handicap,
                 handicap_type, streak_length, endgamefilelist):
        self.white = whitecommand
        self.black = blackcommand
        self.size = size
        self.komi = komi
        self.handicap = handicap
        self.handicap_type = handicap_type
        self.streak_length = streak_length
        self.endgamefilelist = endgamefilelist

    def endgame_contest(self, sgfbase):
        results = []
        i = 1
        for endgamefile in self.endgamefilelist:
            game1 = GTP_game(self.white, self.black, self.size, self.komi,
                             0, "", endgamefile)
            game2 = GTP_game(self.black, self.white, self.size, self.komi,
                             0, "", endgamefile)
            if verbose:
                print "Replaying", endgamefile
                print "Black:", self.black
                print "White:", self.white
            game1.play("")
            result1 = game1.result()[0]
            if result1 != "0":
                plain_result1 = re.search(r"([BW]\+)([0-9]*\.[0-9]*)", result1)
                result1_float = float(plain_result1.group(2))
            else:
                plain_result1 = re.search(r"(0)", "0")
                result1_float = 0.0
            if result1[0] == "B":
                result1_float *= -1
            if verbose:
                print "Result:", result1
                print "Replaying", endgamefile
                print "Black:", self.white
                print "White:", self.black
            game2.play("")
            result2 = game2.result()[1]
            if verbose:
                print "Result:", result2
            if result2 != "0":
                plain_result2 = re.search(r"([BW]\+)([0-9]*\.[0-9]*)", result2)
                result2_float = float(plain_result2.group(2))
            else:
                plain_result2 = re.search(r"(0)", "0")
                result2_float = 0.0

            if result2[0] == "B":
                result2_float *= -1
            results.append(result1_float - result2_float)
            if (result1 != result2):
                print endgamefile+ ":", plain_result1.group(), \
                    plain_result2.group(), "Difference:",
                print result1_float - result2_float
            else:
                print endgamefile+": Same result:", plain_result1.group()
            sgffilename = "%s%03d" % (sgfbase, i)
            game1.writesgf(sgffilename + "_1.sgf")
            game2.writesgf(sgffilename + "_2.sgf")
            game1.quit()
            game2.quit()
            i += 1
        return results

    def play(self, games, sgfbase):
        last_color = ""
        last_streak = 0
        game = GTP_game(self.white, self.black,
                        self.size, self.komi, self.handicap,
                        self.handicap_type, "")
        results = []
        for i in range(games):
            sgffilename = "%s%03d.sgf" % (sgfbase, i + 1)
            game.play(sgffilename)
            result = game.result()
            if result[0] == result[1]:
                print "Game %d: %s" % (i + 1, result[0])
            else:
                print "Game %d: %s %s" % (i + 1, result[0], result[1])

            if result[0][0] == last_color:
                last_streak += 1
            elif result[0][0] != "0":
                last_color = result[0][0]
                last_streak = 1

            if last_streak == self.streak_length:
                if last_color == "W":
                    self.handicap += 1
                    if self.handicap == 1:
                        self.handicap = 2
                    print "White wins too often. Increasing handicap to %d" \
                          % (self.handicap)
                else:   
                    if self.handicap > 0:
                        self.handicap -= 1
                        if self.handicap == 1:
                            self.handicap = 0
                        print "Black wins too often. Decreasing handicap to %d" \
                              % (self.handicap)
                    else:
                        self.handicap = 2
                        game.swap_players()
                        print "Black looks stronger than white. Swapping colors and setting handicap to 2"
                game.set_handicap(self.handicap)
                last_color = ""
                last_streak = 0
            results.append(result)
        cputime = game.cputime()
        game.quit()
        return results, cputime


# ================================================================
#                      Main program
#


# Default values
#

white    = ""
black    = ""
komi     = ""
size     = "19"
handicap = 0
handicap_type = "fixed"
streak_length = -1
endgame_start_at = 0

games   = 1
sgfbase = "twogtp"

verbose = 0

helpstring = """

Run with:

twogtp --white \'<path to program 1> --mode gtp [program options]\' \\
       --black \'<path to program 2> --mode gtp [program options]\' \\
       [twogtp options]

Possible twogtp options:

  --verbose 1 (to list moves) or --verbose 2 (to draw board)
  --komi <amount>
  --handicap <amount>
  --free-handicap <amount>
  --adjust-handicap <length>        (change handicap by 1 after <length> wins
                                     in a row)
  --size <board size>               (default 19)
  --games <number of games to play> (default 1)
  --sgfbase <filename>              (create sgf files with sgfbase as basename)
  --endgame <moves before end>      (endgame contest - add filenames of
                                     games to be replayed after last option)
"""

def usage():
    print helpstring
    sys.exit(1)

try:
    (opts, params) = getopt(sys.argv[1:], "",
                            ["black=",
                             "white=",
                             "verbose=",
                             "komi=",
                             "boardsize=",
                             "size=",
                             "handicap=",
                             "free-handicap=",
                             "adjust-handicap=",
                             "games=",
                             "sgfbase=",
                             "endgame=",
                            ])
except:
    usage();

for opt, value in opts:
    if opt == "--black":
        black = value
    elif opt == "--white":
        white = value
    elif opt == "--verbose":
        verbose = int(value)
    elif opt == "--komi":
        komi = value
    elif opt == "--boardsize" or opt == "--size":
        size = value
    elif opt == "--handicap":
        handicap = int(value)
        handicap_type = "fixed"
    elif opt == "--free-handicap":
        handicap = int(value)
        handicap_type = "free"
    elif opt == "--adjust-handicap":
        streak_length = int(value)
    elif opt == "--games":
        games = int(value)
    elif opt == "--sgfbase":
        sgfbase = value
    elif opt == "--endgame":
        endgame_start_at = int(value)

if endgame_start_at != 0:
    endgame_filelist = params
else:
    endgame_filelist = []
    if params != []:
        usage()
            

if black == "" or white == "":
    usage()

if komi == "":
    if handicap > 1 and streak_length == -1:
        komi = "0.5"
    else:
        komi = "5.5"

match = GTP_match(white, black, size, komi, handicap, handicap_type,
                  streak_length, endgame_filelist)
if endgame_filelist != []:
    results = match.endgame_contest(sgfbase)
    win_black = 0
    win_white = 0
    for res in results:
        print res
        if res > 0.0:
            win_white += 1
        elif res < 0.0:
            win_black += 1
    print "%d games, %d wins for black, %d wins for white." \
         % (len(results), win_black, win_white)

else:
    results, cputimes = match.play(games, sgfbase)

    i = 0
    for resw, resb in results:
        i = i + 1
        if resw == resb:
            print "Game %d: %s" % (i, resw)
        else:
            print "Game %d: %s %s" % (i, resb, resw)
    if (cputimes["white"] != "0"):
        print "White: %ss CPU time" % cputimes["white"]
    if (cputimes["black"] != "0"):
        print "Black: %ss CPU time" % cputimes["black"]
