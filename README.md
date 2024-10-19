Myrddin v0.91 Release Notes -- 10/20/24
--------------------------------------------------------------------------------
Myrddin is a winboard-compliant chess engine to a reasonable degree. It supports protover 1 primarily to support the Chessmaster interface, and also protover 2.

Version 0.91 plays approximately 2900 ELO (CCRL) at 1 CPU, 2980 at 4 CPU against chess engines, and probably 100-200 points higher against humans. This is an improvement of approximately 275 points compared to version 0.90.

The following winboard commands are supported:\
protover\
xboard\
new\
go\
?\
analyze\
playother\
setboard (also "loadfen")\
force\
white/black\
time\
level\
sd\
st\
hard/easy\
cores\
result\
undo\
post/nopost\
computer\
exit\
quit

Myrddin also supports the following non-winboard commands:\
"eval", which returns a static evaluation of the current game position\
"perft N" and "divide N", where 'N' is the perft depth requested\
"tb", which toggles Gaviota endgame tablebase support\
"rpt", runs perft on a pre-defined set of positions, using bulk counting and only one thread\
“see”, which returns the SEE value of a capture on the current position - example usage “see d4 e5”\
None of these commands are supported while Myrddin is searching/analyzing.

Winboard UI notes: \
-- It is crucial that the winboard UI being used send the "time" command to the engine, as Myrddin does not have an internal clock.  
-- Post is ON by default, as opposed to the winboard protocol. This is just for debugging convenience and it appears that a lot of engines do this anyway.

General Notes:\
-- This the first version of Myrddin to use an "NNUE" architecture for position evaluation. The code was graciously provided by David Carteau (Orion).\
-- Myrddin's "very lazy SMP" implementation uses multiple secondary processes to fill the transposition, eval and pawn hash tables so the primary process can search deeper in the same amount of time.\
-- Myrddin uses Pradu Kannan's "magicmoves" code for move generation of sliding pieces.\
-- Search is basic alpha/beta, with reasonable and generally conservative extensions and reductions.\
-- Max search depth is 128. \
-- The ProDeo opening book is used by kind permission of Ed Schröder.\
-- Draw claims from the opponent are not supported, nor does Myrddin know how to claim a draw.\
-- There is just enough winboard support to play games on ICS. But without support for "draw" offers, I suspect there are some scary loopholes and/or exploits. \
-- When the engine is in analysis mode, positive scores always favor White and negative scores always favor Black. When the engine is thinking or pondering, positive scores favor Myrddin.\
–- Logfiles will be in the “logs” folder below the folder where you ran Myrddin. The output of the log is not very interesting – just PV output and communication reality-check stuff. If you are running Myrddin with multiple CPUs, there will be one logfile for each process.

FULL DISCLOSURE: \
Myrddin's Winboard interface is based on Tom Kerrigan's excellent TSCP engine, for which Tom has graciously given permission.

Myrddin chess engine written by John Merlino, with lots of inspiration, assistance and patience (and perhaps some code) by:\
// Ron Murawski - Horizon (great amounts of assistance as I was starting out, plus hosting Myrddin's site)\
// Lars Hallerstrom - The Mad Tester!\
// David Carteau - Orion and Cerebrum (provided Cerebrum NN library)\
// Martin Sedlak - Cheng (guided me through Texel tuning)\
// Dann Corbit - Helped bring the first bitboard version of Myrddin to release\
// Bruce Moreland - Gerbil (well-documented code on his website taught me the basics)\
// Tom Kerrigan - TSCP (a great starting point)\
// Dr. Robert Hyatt - Crafty (helped us all)\
// Ed Schröder and Jeroen Noomen - ProDeo Opening Book\
// Andres Valverde - EveAnn and part of the Dirty team\
// Pradu Kannan - Magicmoves bitboard move generator for sliding pieces\
// Vladimir Medvedev - Greko (showed how strong a small, simple program could be)\
// The Chessmaster Team - Lots of brilliant people, but mostly Johan de Koning (The King engine), Don Laabs, James Stoddard and Dave Cobb

Version List
---------------
Version 0.91 (10/20/24) Change List:\
-- Added NNUE probing code by David Carteau (Orion / Cerebrum). All code related to the HCE has been removed, as well as the pawn hash\
-- Network created by me using games from CCRL, Lichess, and Myrddin testing (both self-play and against other engines)\
-- Fixed a rare bug that could cause the best move from the previous iteration to not be the first move searched\
-- If perft or divide are called with no parameters, the default depth will be one (and Myrddin will no longer crash)

Version 0.90 (6/9/23) Change List:\
-- Fixed two bugs in SEE (stopped the calculation if the first capture was of equal value, and failed to include Kings in the calculation)\
-- Fixed a bug that could cause a save to the hash table even if there was no best move\
-- Tuned PST files for the first time, and re-tuned all other eval terms\
-- Captures with negative SEE value can now be reduced\
-- IID is now more aggressive in its depth reduction and can be applied in PV nodes\
-- LMR reduction is now one depth less for PV nodes\
-- No longer limiting the number of extensions for a single branch\
-- Reduced the number of aspiration windows before performing a full-width search from six to two\
-- Fixed a rare bug such that if a tt probe or IID returned an underpromotion it would not be moved to the front of the movelist\
-- Fixed an issue when receiving the "force" command while pondering, which can happen with some GUIs\
-- Modified the compiler options for magicmoves to improve perft results by 5%.\
-- Various minor optimizations\
-- Added "see" command to return the SEE value of a capture on the current position - example usage "see d4 e5"\
-- Added "rpt" command to run a brief perft test (perft uses bulk counting)\
-- Removed "-64" from version string as there is no longer a 32-bit version

Version 0.89 (6/3/22) Change List:\
-- Added Late Move Pruning\
-- Late Move Reductions are now more aggressive and use a logarithmic formula\
-- Bad captures are now subject to LMR\
-- Moved killer moves before equal captures in move ordering\
-- Moved bad captures after quiet moves in move ordering\
-- Moves that give check are no longer flagged as such at move generation, but instead during MakeMove()\
-- Reduced the number of fail high/low results at the root before searching on full-width window\
-- BitScan now uses an intrinsic function rather than MS Windows' BitScanForward64() - thank you to Pawel Osikowski and Bo Persson for the suggestion!\
-- Null Move now uses a reduction of 3+(depth/6) instead of just 3\
-- Myrddin will now move instantly if TBs are available and <=5 men on board. Previously it would "search" all the way to max depth (128) before moving, causing potential buffer problems with some GUIs at very fast time controls.\
-- Minor evaluation tuning adjustments, most notably adding code for Bishop Outposts and a significant increase for pawns on the 7th rank\
-- Increased the aggressiveness of SMP depth adjustment for child processes

Version 0.88 (7/18/21) Change List:\
-- Added tapered eval\
-- Created rudimentary tuning system and tuned all eval parameters except PSTs (I'm too lazy)\
-- Time management is now less conservative overall\
-- Fixed a bug in which the best move from IID might move farther down in the move order if it was also a killer\
-- Significantly reduced the frequency of checking for input from stdin\
-- Fixed a crash that could occur when making a very long PV string\
-- Fixed a bug that could cause the setboard/loadfen command to fail if the FEN string had any trailing whitespaces\
-- Removed "Mate in N" announcement from PVs during fail highs and fail lows

Version 0.87 (1/20/15) Change List:\
-- Added SMP support for up to 16 instances (using processes and shared hash memory -- Very Lazy SMP!) -- only tested up to 4 instances! This use of shared memory for all hash tables (transposition, eval and pawn) means that the reported memory usage may appear to be incorrect depending on what program you use to get the information. Beware!\
-- Fixed very embarrassing bug in passer eval calculating the distance between two squares that could cause any number of other issues, even a hang or crash\
-- Added support for the "cores" command which changes the number of processes being used when SMP is enabled. The initial value will be based on ini file settings.\
-- Added a "tb" command which toggles tablebase support. The initial value will be based on ini file settings.\
-- Added "Mate in N" announcements to PV output when applicable\
-- Fixed a bug in saving scores near mate to the hash table\
-- Fixed a bug in determining valid knight outpost squares\
-- Fixed text output bug in kibitzing opening book moves on ICC\
-- Fixed a rare bug in determining material draw\
-- Fixed a bug in which Myrddin would hang or crash if the '.' command (used by Chessmaster GUI) was sent while Myrddin was pondering\
-- Fixed a bug that could cause hash memory allocation to fail if more than 1GB was requested. Thanks to Graham Banks for reporting this issue.

Version 0.86 (12/21/12) Change List:\
-- Finished conversion to bitboards from 0x88\
-- Added support for winboard "st" and "sd" commands\
-- Removed lazy eval

Version 0.85 (4/24/11) Change List:\
-- Almost complete rewrite of the evaluation, in particular the pawn structure evaluation and king safety. Also added mobility factor and significantly adjusted many piece table values\
-- Added SEE (thanks, Andrés!)\
-- Added futility pruning\
-- Added resign. See Myrddin's INI file for instructions, as this is off by default\
-- Added second aspiration window at 150 centipawns.\
-- Cleaned up code for how reductions/extensions are used to modify depth in search calls\
-- Will now claim draw by insufficient material, but only checking for bare kings or at most one minor on board\
-- Now generating all moves in quiescent search if side to move is in check\
-- Now searching all moves in order of their move score order, instead of just the first four moves and then taking the rest as they were generated\
-- Some changes to the transposition table replacement strategy\
-- Fixed a bug in which the en passant square was not being passed to the Gaviota tablebase probe\
-- Fixed a bug in which the 50-move draw check was not checking that the drawing move was also checkmate\
-- Fixed a bug in which the flag for a mate threat found during null move was not being stored in the hash table\
-- Fixed a bug in which a hash entry could be saved when the engine was forcibly bailing out of a search \
-- Fixed a bug that could cause Gaviota tablebases folder to not be read properly from the INI file 

Version 0.84 (9/13/10) Change List:\
-- Added Gaviota tablebases (thanks so much to Miguel Ballicora, also the author of the Gaviota chess engine, for making this available!)\
-- Added initialization file for setting hash size, creating log file, turning on kibitz, and tablebase info\
-- Fixed a bug in which a hash probe would not return a hash move if the saved depth was less than the requested depth (thanks to Edmund Moshammer, author of Glass!), so in these cases the hash move would not be used for move ordering\
-- Implemented fail-hard\
-- Tweaked parameters for reductions (yet again)\
-- Adjusted queen value from 900 to 950 and minor piece value from 310 to 320\
-- Adjusted bishop pair bonus from 20 to 30\
-- Removed second aspiration window\
-- Lazy eval is now more conservative\
-- Will now analyze the entire depth if the first move searched fails low, or if first move fails high in a non-mate situation\
-- Increased maximum search time to half of the remaining clock (required due to above item)\
-- Added support for winboard "computer" command -- kibitz PV, book moves and "Mate in N"\
-- Increased max half-moves in a game to 1024 \
-- Fixed a bug in which the first move searched at the root was saved in the hash table as an exact value even if it did not improve alpha\
-- Fixed a bug in which Myrddin would run out of time by thinking indefinitely if it was told it had a negative amount of time on its clock (can happen on ICS due to lag)\
-- Modified the check to see if null move is allowable, previously was if any piece was on board, now only if side to move has at least one piece

Version 0.83 (2/23/10) Change List:\
-- Only search to depth 1 when there is only one legal move and pondering is off\
-- No longer clearing the hash table before starting a depth 1 search\
-- Improved hash replacement strategy\
-- If a search depth was reduced by late-move reductions, and that search improves alpha, now researching at proper depth\
-- Reductions are now less aggressively implemented\
-- Adjusted the second aspiration window from 300 to 110 centipawns\
-- Doubled (or worse) passed pawns after the leading pawn no longer get the passed pawn bonus\
-- Will now play a move in a checkmating line as soon as it is confirmed to be optimal\
-- Removed "Result" from checkmate reporting\
-- Rook and minor vs rook is now hard-coded as a draw -- should be safe in 99.99% of cases\
-- Re-removed bad king safety code\
-- Now using piece square table for knight outposts, rather than a hard-coded bonus\
-- Modified piece square table for kings in opening and middlegame\
-- Added penalty for having no pawns, to help avoid trading down into pawnless endgames (e.g. rook vs. minor)\
-- Sending the '.' command during analysis no longer restarts the analysis from depth 1. The '.' command is still not implemented, though.\
-- Fixed a bug in which most hash moves were not getting placed at the beginning of the move order\
-- Fixed a bug evaluating Black doubled and passed pawns\
-- Fixed a bug in which the initial position was not being checked for draw by repetition\
-- Fixed a bug maintaining wood tables when unmaking a promotion move\
-- Fixed an asymmetrical problem in the bishop piece square table\
-- Fixed a problem with determining if a rook or queen was behind a passed pawn\
-- Fixed a problem with calculating the pawn shield of a king on column h, or a king not on his home row\
-- Fixed a bug updating the board signature after making a null move when en passant was possible\
-- king+rook vs. king+minor is now being scored as a draw

Version 0.82 (9/25/09) Change List:\
–- Removed “Alpha” designation from version number\
–- Commandline parameters are now supported\
–- Increased max search depth to 50\
–- Reduced move generation time by ~15% (perft 6 of initial position on a P4-3.0 went from 44s to 38s)\
–- Now claiming checkmates, 50-move draws and 3-fold repetition draws\
–- Plays a move after completing a depth 3 search if there is only one legal reply\
–- Will not play a move if in the middle of resolving a fail low or fail high at the root\
–- Will never use more than 1/4 of remaining time\
–- Greatly improved time management for bullet games. Version 0.81 could lose as many as 20% of its games on time at 2 minutes per game. Version 0.82 now only loses about 1% of its games on time at 1 minute per game, and only very rarely will lose a game on time at other time controls. Many thanks to Lars Hallerström (The Mad Tester!) for all of his assistance in helping determine if my “fixes” actually improved anything. Myrddin should never lose on time in games with increments.\
–- Quiescence search now only searches recaptures after depth 1\
–- Added promotions to Quiescence search\
–- Added aspiration window\
–- Improved Late Move Reductions parameters\
–- Fixed a bug in the Principal Variation search\
–- Pondering is now OFF by default\
–- Evaluation adjustments:\
 bishops and knights are scored at 310 centipawns, as Myrddin was susceptible to trading two pieces for rook+pawn\
 increased penalty for doubled/tripled pawns\
 for doubled/tripled pawns, added further penalty if they are blocked\
 added larger bonus for passed pawn on the 7th\
 adjusted bishop piece tables to encourage occupation of long(er) diagonals\
 modified the lone king piece table to give larger penalites as the king goes towards the edge/corner\
–- Added knowledge of knight outposts (in enemy territory, supported by pawn, cannot be attacked by enemy pawn)\
–- Piece and wood counting are now done incrementally\
–- Fixed a bug in which the 50-move counter could be set to zero if the move being pondered was a zeroing move and the opponent did not play that move\
–- Fixed a bug in which the engine would go into an endless loop if it reached (max_search_depth + 1) during pondering or analysis\
–- Fixed a bug in which the engine would go into an endless loop if there were no legal moves during analysis\
–- Fixed a bug in the evaluation of Black doubled/tripled pawns\
–- Added code to handle KPvK endings\
–- Added code to recognize various material draws, such as KNNvK\
–- Added code to recognize insufficient mating material for materially winning side (e.g. KNNvKP or KBvKP)\
–- Added code for rook pawn and wrong colored bishop against lone king in promoting corner (from a loss to Sorgenkind in OpenWar)\
–- Now only adding en passant square to hash signature if en passant capture is possible\
–- Added ”?” and ”!” comments to PV output for fail lows and fail highs\
–- Logfiles are now created in a “logs” folder below the Myrddin executable program, as requested by Lars

v0.81 (5/26/09) Change List:\
-- Greatly improved move generation speed (perft 6 on initial position has gone from 57 to 41 seconds on P4-3.0)\
-- Search now pings the input command handler every 8K nodes (about 1/30-1/50 second, depending on position and hardware), so applicable commands can now be entered during search/analysis\
-- Search will allocate extra thinking time when it gets noticeable drop in score\
-- Evaluation improvements -- pawn structure, open and semi-open files for rooks, rooks behind passers, king safety\
-- Added MVV/LVA and PV Move Ordering\
-- Added Killer and History move ordering heuristics\
-- Added Null Move reductions\
-- Added Late Move reductions\
-- Added Opening Book (ProDeo -- Thanks, Ed!)\
-- Added pondering, so "hard" and "easy" commands are now supported\
-- "analyze", "?" and "result" commands are now supported\
-- "level" command is now fully supported for all time control types\
-- Added check for dirty pawn structure before evaluation\
-- Search can be interrupted due to time management considerations\
-- Added Hash Tables, with a fixed size of 128MB\
-- Maximum search depth increased from 20 to 30\
-- Fixed a bug with evaluating castling moves\
-- Fixed a bug determining which King piece table to use\
-- Can now generate capturing moves only so quiescent search has less moves to sort/deal with\
-- Quiescent moves are added to PV\
-- Added code for pushing lone King towards edge of board\
-- Added material and 50-move draw detection\
-- Fixed a bug in the "undo" and "remove" commands\
-- Scores in analyze mode will always be positive if White is winning and negative if Black is winning\
-- Fixed some bugs in the 3-fold repetition detection\
-- Fixed a stupid bug that caused me to check for user input WAY too often -- many thanks to Bob Hyatt for pointing me in the right direction\
-- Significant code cleanup

v0.80 (3/9/09)\
Initial Release

Please send bug reports and general suggestions/comments to JohnVMerlino@gmail.com.\
If you wish to enter Myrddin into one of your own tournaments, please feel free to do so! But please let me know so I can follow the tournament.

Thanks for playing!
