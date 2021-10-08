# Myrddin
Myrddin is an XBoard/WinBoard compatible chess engine written in C. 
It supports protover 1 primarily to support the Chessmaster interface, and also protover 2. 

Included in this repository are two modules graciously provided by other authors and protected by the licenses stated in their code:
- magicmoves.c and magicmoves.h by Pradu Kannan - bitboard move generation for sliding pieces
- book.cpp and book.h by Ed Schröder - ProDeo opening book probing code 

NOT included in this repository is the code by Miguel Ballicora to support Gaviota tablebases. You can find information about them and download links at https://sites.google.com/site/gaviotachessengine/Home/endgame-tablebases-1

Myrddin Version 0.88 plays approximately 2570 ELO (CCRL - https://ccrl.chessdom.com/ccrl/4040/rating_list_all.html) at 1 CPU and 2670 at 4 CPU against chess engines, and probably 100-200 points higher against humans. This is an improvement of approximately 110 points compared to version 0.87.

The following winboard commands are supported:
- protover
- xboard
- new
- go
- ?
- analyze
- playother
- setboard (also "loadfen")
- force
- white/black
- time
- level
- sd
- st
- hard/easy
- cores
- result
- undo
- post/nopost
- computer
- exit
- quit

Myrddin also supports the following non-winboard commands:
- "eval", which returns a static evaluation of the current game position 
- "perft N" and "divide N", where 'N' is the perft depth requested
- "tb", which toggles Gaviota endgame tablebase support
None of these commands are supported while Myrddin is searching/analyzing.

Winboard UI notes: 
- It is crucial that the winboard UI being used send the "time" command to the engine, as Myrddin does not have an internal clock.  
- Post is ON by default, as opposed to the winboard protocol. This is just for debugging convenience and it appears that a lot of engines do this anyway.

General Notes:
- Myrddin's "very lazy SMP" implementation uses multiple secondary processes to fill the transposition, eval and pawn hash tables so the primary process can search deeper in the same amount of time.
- Myrddin uses Pradu Kannan's "magicmoves" code for move generation of sliding pieces.
- This the first version of Myrddin to have a tapered eval, and the eval was hand-tuned using a very rudimentary Texel tuning system.
- Search is basic alpha/beta, with reasonable and generally conservative extensions and reductions.
- Max search depth is 128. 
- The ProDeo opening book is used by kind permission of Ed Schröder.
- Draw claims from the opponent are not supported, nor does Myrddin know how to claim a draw.
- There is just enough winboard support to play games on ICS. But without support for "draw" offers, I suspect there are some scary loopholes and/or exploits. 
- When the engine is in analysis mode, positive scores always favor White and negative scores always favor Black. When the engine is thinking or pondering, positive scores favor Myrddin.
– Logfiles will be in the “logs” folder below the folder where you ran Myrddin. The output of the log is not very interesting – just PV output and communication reality-check stuff. If you are running Myrddin with multiple CPUs, there will be one logfile for each process.

FULL DISCLOSURE: 
Myrddin's Winboard interface is based on Tom Kerrigan's excellent TSCP engine, for which Tom has graciously given permission.

Many thanks to the following brilliant people, in no particular order, who helped/guided me (either directly or indirectly) in countless ways with their work:
- Ron Murawski - Horizon (great amounts of assistance as I was starting out, plus hosting Myrddin's download site at http://computer-chess.org/doku.php?id=computer_chess:engines:myrddin:index)
- Martin Sedlak - Cheng (guided me through tuning)
- Lars Hallerstrom - The Mad Tester!
- Dann Corbit - instrumental in bringing the first bitboard version to release
- Bruce Moreland - Gerbil (well-documented and explained code on his website taught me the basics)
- Tom Kerrigan - TSCP (a great starting point)
- Dr. Robert Hyatt - Crafty (helped us all)
- Miguel Ballicora - Gaviota Endgame Tablebases
- Ed Schröder and Jeroen Noomen - ProDeo Opening Book
- Andres Valverde - EveAnn and part of the Dirty team
- Pradu Kannan - Magicmoves bitboard move generator for sliding pieces
- Vladimir Medvedev - Greko (showed how strong a small, simple program could be)
- The Chessmaster Team - Lots of brilliant people who got me started loving computer chess, but mostly Johan de Koning (The King engine), Don Laabs, James Stoddard and Dave Cobb
