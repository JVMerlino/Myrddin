/*
Myrddin XBoard / WinBoard compatible chess engine written in C
Copyright(C) 2024  John Merlino

This program is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see < https://www.gnu.org/licenses/>.
*/

enum engine_states
{
    ENGINE_IDLE = 0,	// engine doing nothing, waiting for input
    ENGINE_THINKING,	// engine thinking about a move
    ENGINE_PONDERING,	// engine pondering during its opponent's time
    ENGINE_ANALYZING	// engine infinitely analyzing a position
};

enum thinking_modes
{
    NO_COMMAND = 0,		// act normally
    END_THINKING,		// end thinking by rewinding the search tree
    STOP_THINKING,		// kill thinking immediately -> USE WITH CAUTION, will leave search tree corrupted!
    PONDER,				// tell the engine to begin pondering the next time through the main loop, only used when playing a game
};

typedef struct 
{
    CHESSMOVE	cmKiller;
    long		nEval;
} KILLER, *PKILLER;

extern unsigned long long nSearchNodes, nQNodes, nPerftMoves;
extern PV  evalPV, prevDepthPV;
extern int nCurEval, nPrevEval;
extern BB_BOARD bbEvalBoard;

unsigned long long doBBPerft(int depth, BB_BOARD *Board, BOOL bDivide);
int		Think(int nDepth);
void	ClearHistory(void);
void	ClearKillers(BOOL bScoreOnly);
void    InitThink(void);
int     BBSEEMove(CHESSMOVE* cmMove, int ctSide);
