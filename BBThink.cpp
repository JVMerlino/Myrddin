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

#include <math.h>
#include "Myrddin.h"
#include "Bitboards.h"
#include "MoveGen.h"
#include "Eval.h"
#include "Think.h"
#include "FEN.h"
#include "Hash.h"

#if USE_EGTB
#include "TBProbe.h"
#endif

#define FULL_LOG		FALSE
#define LOG_SEE			FALSE

#define USE_QS_RECAPTURE	FALSE
#define QS_FULL_DEPTH		4		// if USE_QS_RECAPTURE is TRUE, number of plies in qsearch to fully check after which check only recaptures (and promotions)

unsigned long long  nSearchNodes, nQNodes;
unsigned long long  nPerftMoves;
int		nEvalPly, nEvalMove;
int		nQuiesceDepth;
int		nPrevEval, nCurEval;
PV		evalPV, prevDepthPV;
BOOL	bKeepThinking, bIsNullOk, bThinkUntilSafe;
const int		nPieceVals[NPIECES] = { KING_VAL, QUEEN_VAL, ROOK_VAL, MINOR_VAL, MINOR_VAL, PAWN_VAL };  // only used for SEE and move ordering

int LMRReductions[32][32];

#if USE_IMPROVING
int nEvalStack[MAX_DEPTH + 10];
#endif

BB_BOARD	bbEvalBoard;

CHESSMOVE	cmEvalGameMoveList[MAX_MOVE_LIST];

// total time on dev machine with bulk counting = 49.3s (45.7 if not using incremental accumulator update)
PERFT_TEST	perft_tests[NUM_PERFT_TESTS] =
{
"r3k2r/8/8/8/3pPp2/8/8/R3K1RR b KQkq e3 0 1", 6, 485647607,
"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033,
"8/7p/p5pb/4k3/P1pPn3/8/P5PP/1rB2RK1 b - d3 0 28", 6, 38633283,
"8/3K4/2p5/p2b2r1/5k2/8/8/1q6 b - - 1 67", 7, 493407574,
"rnbqkb1r/ppppp1pp/7n/4Pp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", 6, 244063299,
"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 5, 193690690,	// KiwiPete
"8/p7/8/1P6/K1k3p1/6P1/7P/8 w - -", 8, 8103790,
"n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - -", 6, 71179139,
"r3k2r/p6p/8/B7/1pp1p3/3b4/P6P/R3K2R w KQkq -", 6, 77054993,
"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 7, 178633661,
"8/5p2/8/2k3P1/p3K3/8/1P6/8 b - -", 8, 64451405,
"r3k2r/pb3p2/5npp/n2p4/1p1PPB2/6P1/P2N1PBP/R3K2R w KQkq -", 5, 29179893
};

#if USE_KILLERS
static KILLER		cmKillers[MAX_DEPTH + 2][MAX_KILLERS];
#endif

#if USE_HISTORY
static int	cmHistory[64][64];
#endif

/*========================================================================
** doBBPerft - calculates the number of leaf nodes of a given depth from
** the current board position
**========================================================================
*/
unsigned long long doBBPerft(int depth, BB_BOARD* Board, BOOL bDivide)
{
	int			nMove;
	unsigned long long nodes = 0;
	WORD		nNumMoves;
	CHESSMOVE	cmPerftMoveList[MAX_LEGAL_MOVES];

#if !USE_BULK_COUNTING
	if (depth == 0)
		return(1);
#endif

	BBGenerateAllMoves(Board, cmPerftMoveList, &nNumMoves, FALSE);

#if USE_BULK_COUNTING
	if (depth == 1)
		return(nNumMoves);
#endif

	for (nMove = 0; nMove < nNumMoves; nMove++)
	{
		unsigned long long tempnodes;

		char	buf1[16], buf2[16];

		if ((depth > 1) && bDivide)
		{
			BBSquareName(cmPerftMoveList[nMove].fsquare, buf1);
			BBSquareName(cmPerftMoveList[nMove].tsquare, buf2);
			printf("    %s to %s ", buf1, buf2);
		}

#if VERIFY_BOARD
		BB_BOARD	BoardTemp;
		memcpy(&BoardTemp, Board, sizeof(BB_BOARD));
#endif

		BBMakeMove(&cmPerftMoveList[nMove], Board, FALSE);

		tempnodes = doBBPerft(depth - 1, Board, FALSE);

		if ((depth > 1) && bDivide)
			printf("= %I64u nodes\n", tempnodes);
		nodes += tempnodes;

		BBUnMakeMove(&cmPerftMoveList[nMove], Board, FALSE);

#if VERIFY_BOARD
		assert(memcmp(&BoardTemp, Board, sizeof(BB_BOARD)) == 0);
#endif
	}

	return(nodes);
}

/*========================================================================
** PositionRepeated - Checks to see if the position on the eval board has
** ever occurred in the game
**========================================================================
*/
static inline BOOL EvalPositionRepeated(PosSignature dwSignature)
{
	int	n;

	for (n = nEvalMove - 3; n >= 0; n -= 2)
	{
		if (cmEvalGameMoveList[n].dwSignature == dwSignature)
			return(TRUE);
	}

	if (dwSignature == dwInitialPosSignature)
		return(TRUE);

	return(FALSE);
}

/*========================================================================
** CheckTimeRemaining - if we've used too much time thinking about this
** move, tell the engine to end thinking and pick a move by returning FALSE
**========================================================================
*/
static inline BOOL CheckTimeRemaining(void)
{
#if USE_SMP
	if (bSlave)
		return(TRUE);
#endif

	BOOL		    bTimeRemaining;
	int			    nEvalDip;

	if ((nEngineMode == ENGINE_ANALYZING) || (nEngineMode == ENGINE_PONDERING))
		return(TRUE);

	ULONGLONG gtc = GetTickCount64();

	ULONGLONG nTimeUsed = gtc - nThinkStart - nPonderTime;

	if (bExactThinkTime)
	{
		if ((gtc - nThinkStart) >= (ULONGLONG)(nThinkTime * 1000))
			return(FALSE);
		else
			return(TRUE);
	}

	if (bExactThinkNodes)
		return(nThinkNodes > nSearchNodes);

	if (bExactThinkDepth)
		return(TRUE);

	if (nTimeUsed > ((nClockRemaining + nFischerInc) >> 1))	// never use more than 1/2 of the remaining time
		return(FALSE);

	if (bKeepThinking || bThinkUntilSafe)
		return(TRUE);

	bTimeRemaining = (nTimeUsed < nThinkTime);

	if (bTimeRemaining)
		return(TRUE);

	if ((nCurEval == NO_EVAL) && (prevDepthPV.pvLength == 0))
		return(TRUE);	// we haven't picked a move yet, so we can't bail

	// we're out of time, but do we still need to keep thinking because we might be in trouble?

	if (nCurEval == NO_EVAL)	// we don't have an eval at the root for this iteration, so we can bail
		return(FALSE);

	if (nCurEval >= nPrevEval)	// current eval is no worse than previous iteration eval
		return(FALSE);

	if (nCurEval > 200)	// we're still way up in eval, so it's ok to end
		return(FALSE);

	nEvalDip = nPrevEval - nCurEval;

	if (nEvalDip <= 10)	// very small dip, so we can bail
		return(FALSE);

	if ((nEvalDip <= 50) && (nCurEval >= 150))	// we're not TOO much worse, and we're still well ahead, so no problem
		return(FALSE);

	// ok, things are potentially looking bad, so we'll need to extend the time

	if ((nEvalDip > 10) && (nEvalDip <= 25))
	{
		if (nCurEval <= 50)
			return(nTimeUsed <= (nThinkTime * 3 / 2));	// we're only slightly worse and losing (or not winning by much)
		// so allow for 1.5x time
		else
			return(FALSE);	// we're far enough ahead that it's no problem
	}

	if ((nEvalDip > 25) && (nEvalDip <= 50))
	{
		if (nCurEval <= 100)
			return(nTimeUsed <= (nThinkTime * 2)); // we're quite a bit worse, so allow for 2x time
		else
			return(nTimeUsed <= (nThinkTime * 3 / 2)); // we're quite a bit worse, but still up a decent amount
	}

	if ((nEvalDip > 50) && (nEvalDip <= 100))
	{
		if (nCurEval <= 100)
			return(nTimeUsed <= (nThinkTime * 4)); // we've dropped as much as a pawn, so allow for 4x time
		else
			return(nTimeUsed <= (nThinkTime * 2)); // we've dropped as much as a pawn, but we're still up pretty well
	}

	if (nEvalDip > 100)
	{
		if (nCurEval <= 100)
			return(nTimeUsed <= (unsigned int)(nClockRemaining >> 1));	// we've dropped more than a pawn, use up to half time remaining
		else
			return(nTimeUsed <= (nThinkTime * 4));	// we've dropped more than a pawn, but we're still up
	}

	// keep thinking!
	if (bLog)
		fprintf(logfile, "we shouldn't get here!\n");

	return(TRUE);
}

/*========================================================================
** GetNextMove - finds the move with the highest score in the move list
**========================================================================
*/
static inline CHESSMOVE* GetNextMove(CHESSMOVE* MoveList, int nNumMoves)
{
	int			n, nBest;
	long 		nBestScore;

	assert(nNumMoves > 0);

	nBest = 0;
	nBestScore = MoveList[0].nScore;

	while (MoveList[nBest].moveflag & MOVE_SEARCHED)
	{
		nBest++;
		nBestScore = MoveList[nBest].nScore;
	}

	// find the move with the highest score
	for (n = nBest + 1; n < nNumMoves; n++)
	{
		if ((MoveList[n].nScore > nBestScore) && !(MoveList[n].moveflag & MOVE_SEARCHED))
		{
			nBest = n;
			nBestScore = MoveList[n].nScore;
		}
	}

	MoveList[nBest].moveflag |= MOVE_SEARCHED;
	return(&MoveList[nBest]);
}

/*========================================================================
** ScoreMoves - update moves based on scores from killer and history
** heuristics
**========================================================================
*/
static inline void ScoreMoves(CHESSMOVE* MoveList, int nNumMoves)
{
	int	n;
	CHESSMOVE cmMove;

	for (n = 0; n < nNumMoves; n++)
	{
		cmMove = MoveList[n];

#if USE_HISTORY
		MoveList[n].nScore += cmHistory[cmMove.fsquare][cmMove.tsquare];	// update from the history array
#endif

#if USE_SEE_MOVE_ORDER
		if (cmMove.moveflag & MOVE_CAPTURE)
			cmMove.nScore = MoveList[n].nScore += BBSEEMove(&cmMove, nSideToMove);
#endif

#if USE_KILLERS
		if (cmMove.nScore >= KILLER_1_SORT_VAL)
			continue;

		// update scores based on killers array
		if ((cmMove.fsquare == cmKillers[nEvalPly][0].cmKiller.fsquare) &&
			(cmMove.tsquare == cmKillers[nEvalPly][0].cmKiller.tsquare) /* &&
			(cmMove.moveflag == cmKillers[nEvalPly][0].cmKiller.moveflag) */)
		{
			MoveList[n].nScore = KILLER_1_SORT_VAL;
#if 0
			if (abs(cmKillers[nEvalPly][0].nEval) > (CHECKMATE / 2))
				MoveList[n].nScore += MATE_KILLER_BONUS;
#endif
		}

#if (MAX_KILLERS > 1)
		if ((cmMove.fsquare == cmKillers[nEvalPly][1].cmKiller.fsquare) &&
			(cmMove.tsquare == cmKillers[nEvalPly][1].cmKiller.tsquare) /* &&
			(cmMove.moveflag == cmKillers[nEvalPly][1].cmKiller.moveflag) */)
		{
			MoveList[n].nScore = KILLER_2_SORT_VAL;
#if 0
			if (abs(cmKillers[nEvalPly][1].nEval) > (CHECKMATE / 2))
				MoveList[n].nScore += MATE_KILLER_BONUS;
#endif
		}
#endif

#if (MAX_KILLERS > 2)
		if ((cmMove.fsquare == cmKillers[nEvalPly][2].cmKiller.fsquare) &&
			(cmMove.tsquare == cmKillers[nEvalPly][2].cmKiller.tsquare) /* &&
			(cmMove.moveflag == cmKillers[nEvalPly][2].cmKiller.moveflag) */)
		{
			MoveList[n].nScore = KILLER_3_SORT_VAL;
#if 0
			if (abs(cmKillers[nEvalPly][2].nEval) > (CHECKMATE / 2))
				MoveList[n].nScore += MATE_KILLER_BONUS;
#endif
		}
#endif
#endif
	}
}

#if USE_KILLERS
/*========================================================================
** UpdateKiller - add a killer move to the killer list
**========================================================================
*/
static inline void UpdateKiller(int nPly, CHESSMOVE* cmKiller, int nEval)
{
	// check to see if the move is already in the list
	if ((cmKiller->fsquare == cmKillers[nPly][0].cmKiller.fsquare) && (cmKiller->tsquare == cmKillers[nPly][0].cmKiller.tsquare))
		return;
#if (MAX_KILLERS > 1)
	if ((cmKiller->fsquare == cmKillers[nPly][1].cmKiller.fsquare) && (cmKiller->tsquare == cmKillers[nPly][1].cmKiller.tsquare))
		return;
#if (MAX_KILLERS > 2)
	if ((cmKiller->fsquare == cmKillers[nPly][2].cmKiller.fsquare) && (cmKiller->tsquare == cmKillers[nPly][2].cmKiller.tsquare))
		return;
#endif
#endif

	if (nEval > cmKillers[nPly][0].nEval)
	{
#if (MAX_KILLERS > 1)
		cmKillers[nPly][1] = cmKillers[nPly][0];
#endif
		cmKillers[nPly][0].cmKiller = *cmKiller;
		cmKillers[nPly][0].nEval = nEval;
	}
#if (MAX_KILLERS > 1)
	else if (nEval > cmKillers[nPly][1].nEval)
	{
#if (MAX_KILLERS > 2)
		cmKillers[nPly][2] = cmKillers[nPly][1];
#endif
		cmKillers[nPly][1].cmKiller = *cmKiller;
		cmKillers[nPly][1].nEval = nEval;
	}
#if (MAX_KILLERS > 2)
	else if (nEval > cmKillers[nPly][2].nEval)
	{
		cmKillers[nPly][2].cmKiller = *cmKiller;
		cmKillers[nPly][2].nEval = nEval;
	}
#endif
#endif
}

/*========================================================================
** ClearKillers - clear the killer array
**========================================================================
*/
void ClearKillers(BOOL bScoreOnly)
{
	int	ply, killer;

	if (!bScoreOnly)
		ZeroMemory(&cmKillers, sizeof(cmKillers));

	for (ply = 0; ply < MAX_DEPTH + 2; ply++)
	{
		for (killer = 0; killer < MAX_KILLERS; killer++)
			cmKillers[ply][killer].nEval = -MAX_WINDOW;
	}
}
#endif

#if USE_HISTORY
/*========================================================================
** UpdateHistory - add a move to the history array
**========================================================================
*/
static void UpdateHistory(CHESSMOVE* cmMove, int nDepth)
{
	int	x, y;

	cmHistory[cmMove->fsquare][cmMove->tsquare] += nDepth * nDepth;

	// readjust all history scores if one gets too big
	if (cmHistory[cmMove->fsquare][cmMove->tsquare] > MAX_HISTORY_VAL)
	{
		for (x = 0; x < 64; x++)
			for (y = 0; y < 64; y++)
				cmHistory[x][y] /= 2;
	}
}

/*========================================================================
** ClearHistory - clear the history array
**========================================================================
*/
void ClearHistory(void)
{
	ZeroMemory(cmHistory, sizeof(cmHistory));
}
#endif

#if USE_NULL_MOVE
/*========================================================================
** IsNullOk -- check to see if it's ok to use null move
**========================================================================
*/
static inline BOOL BBIsNullOk(void)
{
	// null move is ok if the side to move has any pieces left
	if (bbEvalBoard.sidetomove == WHITE)
	{
		return(bbEvalBoard.bbPieces[QUEEN][WHITE] ||
			bbEvalBoard.bbPieces[ROOK][WHITE] ||
			bbEvalBoard.bbPieces[BISHOP][WHITE] ||
			bbEvalBoard.bbPieces[KNIGHT][WHITE]);
	}
	else // black to move
	{
		return(bbEvalBoard.bbPieces[QUEEN][BLACK] ||
			bbEvalBoard.bbPieces[ROOK][BLACK] ||
			bbEvalBoard.bbPieces[BISHOP][BLACK] ||
			bbEvalBoard.bbPieces[KNIGHT][BLACK]);
	}
}
#endif

/*========================================================================
** SEE - recursive function to determine if a square is threatened by a
** piece, using lowest valued pieces first
**========================================================================
*/
static inline int BBSEE(SquareType sqTarget, int captured, int ctSide)
{
	int			sqFrom;
	int			val, seeVal, piece;
	Bitboard	attacker;
	Bitboard	attackers = GetAttackers(&bbEvalBoard, sqTarget, ctSide, FALSE);

	if (attackers == BB_EMPTY)
		return(0);

	val = 0;  // default value

	sqFrom = NO_SQUARE;
	for (piece = PAWN; piece >= KING; piece--)	// no Kings in SEE?
	{
		attacker = attackers & bbEvalBoard.bbPieces[piece][ctSide];
		if (attacker)
		{
			sqFrom = BitScan(PopLSB(&attacker));
			break;
		}
	}

	if (sqFrom == NO_SQUARE)
		return(val);

#if LOG_SEE
	if (bLog)
		fprintf(logfile, "\tSquare %02X attacked by piece at square %02X\n", sqTarget, sqFrom);
#endif

	// remove the attacker from the board
	assert(piece >= 0 && piece < 6);
	assert(ctSide >= 0 && ctSide < 2);
	bbEvalBoard.bbPieces[piece][ctSide] &= ~Bit[sqFrom];
	bbEvalBoard.bbOccupancy &= ~Bit[sqFrom];

	// recurse on same square with sides changed
	seeVal = nPieceVals[captured] - BBSEE(sqTarget, piece, OPPONENT(ctSide));

	// put the attacker back on the board
	bbEvalBoard.bbPieces[piece][ctSide] |= Bit[sqFrom];
	bbEvalBoard.bbOccupancy |= Bit[sqFrom];

	val = max(0, seeVal);

	return(val);
}

/*========================================================================
** SEEMove - determine if a move (usually a capture) loses material
**========================================================================
*/
int BBSEEMove(CHESSMOVE* cmMove, int ctSide)
{
	int		val;
	int		capturer, captured;

#if LOG_SEE
	if (bLog)
	{
		char buffer[256];

		buffer[0] = '\0';
		fprintf(logfile, "\nSee Move of capture %02X to %02X\n", cmMove->fsquare, cmMove->tsquare);
		BBBoardToForsythe(&bbEvalBoard, 0, buffer);
		fprintf(logfile, "Board is %s\n", buffer);
	}
#endif

	capturer = bbEvalBoard.squares[cmMove->fsquare];
	captured = bbEvalBoard.squares[cmMove->tsquare];

	IS_PIECE_OK(PIECEOF(capturer));
	IS_PIECE_OK(PIECEOF(captured));

	// there's no need to perform SEE on a move in which the first capture is
	// made by a piece that is of lesser value than the captured piece
	if (nPieceVals[PIECEOF(capturer)] < nPieceVals[PIECEOF(captured)])
		return(0);

	// make the move
	BBMakeMove(cmMove, &bbEvalBoard, FALSE);

	// Handle promotions???!!!

	// result is Piece_Just_Captured minus SEE of destination square
	val = nPieceVals[PIECEOF(captured)] - BBSEE(cmMove->tsquare, PIECEOF(capturer), OPPONENT(ctSide));

#if LOG_SEE
	if (bLog)
		fprintf(logfile, "See value is %d\n", val);
#endif

	// unmake the move
	BBUnMakeMove(cmMove, &bbEvalBoard, FALSE);

	return(val);
}

/*========================================================================
** Quiesce - Quiescent search extension using captures and promotions only
**========================================================================
*/
#if USE_QS_RECAPTURE
int BBQuiesce(int nAlpha, int nBeta, PV* pvLine, SquareType sqTarget)
#else
static int BBQuiesce(int nAlpha, int nBeta, PV* pvLine)
#endif
{
	WORD	nNumLegalMoves;
	int		n;
	PV		pv;
	int		nEval, nStandPat;
	BOOL	bInCheck = bbEvalBoard.inCheck;
	CHESSMOVE cmEvalMoveListQ[MAX_LEGAL_MOVES];

	assert(bInCheck == BBKingInDanger(&bbEvalBoard, bbEvalBoard.sidetomove));

	if (nQuiesceDepth)
	{
		nSearchNodes++;
#if SHOW_QS_NODES
		nQNodes++;
#endif
#if USE_SMP
		if (bSlave)
			smSharedMem->sdSlaveData[nSlaveNum].nSearchNodes++;
#endif
	}

	if ((nSearchNodes & nCheckNodes) == 0)
	{
		if (CheckForInput(FALSE))
			HandleCommand();

		if ((CheckTimeRemaining() == FALSE) && (nEngineCommand != STOP_THINKING))	// a command might tell us to stop
			nEngineCommand = END_THINKING;
	}

	if (nEngineCommand == END_THINKING)	// out of time or "move now"
		return(0);

	if (nEngineCommand == STOP_THINKING)	// just stop thinking and return to the main loop as quickly as possible, losing all search info
		return(0);

#if USE_MATE_DISTANCE_PRUNING
	// mate distance pruning
	int nMateValue = CHECKMATE - nEvalPly;
	if (nMateValue < nBeta)
	{
		nBeta = nMateValue;
		if (nAlpha >= nMateValue)
			return(nAlpha);
	}
	nMateValue = -CHECKMATE + nEvalPly;
	if (nMateValue > nAlpha)
	{
		nAlpha = nMateValue;
		if (nBeta <= nMateValue)
			return(nBeta);
	}
#endif

#if USE_HASH_IN_QS
	// probe the hash table
	int			nHashFlags = 0;
	BYTE		nHashType = HASH_ALPHA;
	HASH_ENTRY* heHash = ProbeHash(bbEvalBoard.signature);

	if ((heHash != NULL) && (nEngineMode == ENGINE_PONDERING ? nEvalPly >= 3 : nEvalPly >= 2))
	{
#if FULL_LOG
		if (nEvalPly == 0)
		{
			fprintf(logfile, "Got Hash Entry at eval ply %d -- nEval %d, from 0x%02X, to 0x%02X, flags %08X\n",
				nEvalPly, heHash->h.nEval, heHash->h.from, heHash->h.to, heHash->h.nFlags);
			fflush(logfile);
		}
#endif
		nHashFlags = heHash->h.nFlags;

//		if (heHash->h.nDepth >= nDepth)
		{
			int nHashEval = heHash->h.nEval;

			if (nHashEval >= (CHECKMATE / 2))
				nHashEval -= nEvalPly;
			else if (nHashEval < -(CHECKMATE / 2))
				nHashEval += nEvalPly;

			if (nHashFlags & HASH_EXACT)
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Exact\n");
					fflush(logfile);
				}
#endif
				if (nHashEval >= nBeta)
					return(nBeta);
				if (nHashEval <= nAlpha)
					return(nAlpha);
				return nHashEval;
			}

			if ((nHashFlags & HASH_ALPHA) && (nHashEval <= nAlpha))
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Alpha\n");
					fflush(logfile);
				}
#endif
				return nAlpha;
			}
			else if ((nHashFlags & HASH_BETA) && (nHashEval >= nBeta))
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Beta\n");
					fflush(logfile);
				}
#endif
				return nBeta;
			}
		}
	}
#endif // USE_HASH_IN_QS

	nStandPat = BBEvaluate(&bbEvalBoard, nAlpha, nBeta);

	if (nEvalPly >= MAX_DEPTH)
	{
		pvLine->pvLength = 0;
		return(nStandPat);
	}

	if (!bInCheck)
	{
		if (nStandPat >= nBeta)
		{
			pvLine->pvLength = 0;
			return(nBeta);
		}

		if (nStandPat > nAlpha)
			nAlpha = nStandPat;
	}
	pv.pvLength = 0;

	BBGenerateAllMoves(&bbEvalBoard, &cmEvalMoveListQ[0], &nNumLegalMoves, !bInCheck);

	if (nNumLegalMoves == 0)
	{
		pvLine->pvLength = 0;
		return(nStandPat);
	}

	//  ScoreMoves(&cmEvalMoveList[0], nNumMoves);	// not necessary unless ScoreMoves() changes!

	for (n = 0; n < nNumLegalMoves; n++)
	{
		CHESSMOVE	cmMove;

		cmMove = *GetNextMove(&cmEvalMoveListQ[0], nNumLegalMoves);

		assert(bInCheck || (cmMove.moveflag & (MOVE_CAPTURE | MOVE_PROMOTED)));
		if (!bInCheck && ((cmMove.moveflag & (MOVE_CAPTURE | MOVE_PROMOTED)) == 0))
			continue;

		// only check promotions to queen
		if ((cmMove.moveflag & MOVE_PROMOTED) && (PIECEOF(cmMove.moveflag) != QUEEN))
			continue;

#if USE_QS_RECAPTURE
		if (((cmMove.moveflag & MOVE_PROMOTED) == 0) && (nQuiesceDepth >= QS_FULL_DEPTH) &&
			(cmMove.tsquare != sqTarget) && (sqTarget != NO_SQUARE) && !bInCheck)
		{
			continue;
		}
#endif

		// futility pruning
		if (!bInCheck)
		{
#if USE_FUTILITY_PRUNING
			int	nFutile = PAWN_VAL;
			if (cmMove.moveflag & MOVE_PROMOTED)
				nFutile = QUEEN_VAL;
			if (cmMove.moveflag & MOVE_CAPTURE)
			{
				if (cmMove.moveflag & MOVE_ENPASSANT)
					nFutile += PAWN_VAL;
				else
					nFutile += nPieceVals[PIECEOF(bbEvalBoard.squares[cmMove.tsquare])];
			}
			if (nStandPat + nFutile < nAlpha)
				continue;
#endif

#if USE_SEE
			if (cmMove.moveflag & MOVE_CAPTURE)
				//			if ((cmMove.moveflag & MOVE_CAPTURE) /* && (n > 0) */ && ((cmMove.moveflag & MOVE_PROMOTED) == 0))
			{
				int	nSee;

#if VERIFY_BOARD
				BB_BOARD	BoardTemp;
				memcpy(&BoardTemp, &bbEvalBoard, sizeof(BB_BOARD));
#endif

				nSee = BBSEEMove(&cmMove, bbEvalBoard.sidetomove);

#if VERIFY_BOARD
				assert(memcmp(&BoardTemp, &bbEvalBoard, sizeof(BB_BOARD)) == 0);
#endif

				if (nSee < 0)
					continue;
			}
#endif
		}

		BBMakeMove(&cmMove, &bbEvalBoard, TRUE);
		cmMove.dwSignature = bbEvalBoard.signature;	// bbEvalBoard.signature;
		cmEvalGameMoveList[nEvalMove++] = cmMove;
		nEvalPly++;
		nQuiesceDepth++;

#if USE_QS_RECAPTURE
		nEval = -BBQuiesce(-nBeta, -nAlpha, &pv, cmMove.tsquare);
#else
		nEval = -BBQuiesce(-nBeta, -nAlpha, &pv);
#endif

		BBUnMakeMove(&cmMove, &bbEvalBoard, TRUE);
		nEvalMove--;
		nEvalPly--;
		nQuiesceDepth--;

		if ((nEngineCommand == END_THINKING) || (nEngineCommand == STOP_THINKING))
			break;

		if (nEval > nAlpha)
		{
			nAlpha = nEval;

			/* update the PV */
			if (pvLine)
			{
				pvLine->pv[0].fsquare = cmMove.fsquare;
				pvLine->pv[0].tsquare = cmMove.tsquare;
				pvLine->pv[0].moveflag = cmMove.moveflag;
				memcpy(&pvLine->pv[1], pv.pv, pv.pvLength * sizeof(CHESSMOVE));
				pvLine->pvLength = pv.pvLength + 1;
			}

			if (nEval >= nBeta)
				return(nBeta);
		}
	}

	return(nAlpha);
}

/*========================================================================
** AlphaBeta - Standard Alpha/Beta search with PV capture
**========================================================================
*/
static int BBAlphaBeta(int nDepth, int nAlpha, int nBeta, PV* pvLine, BOOL bNullMove)
{
	WORD	nNumMoves, n;
	int		nEval = 0;
	PV		pv;
	int		nReductions = 0;
	BOOL	bInCheck = bbEvalBoard.inCheck;
	BOOL    bNullMateThreat = FALSE;
	CHESSMOVE	cmBestMove;
	CHESSMOVE	cmEvalMoveList[MAX_LEGAL_MOVES];

	//    assert(bInCheck == BBKingInDanger(&bbEvalBoard, bbEvalBoard.sidetomove));

	nSearchNodes++;

#if FULL_LOG
	if (nEvalPly == 0)
	{
		fprintf(logfile, "Calling Alpha Beta -- Depth %d, Alpha %d, Beta %d, Null %d, nEvalPly %d\n", nDepth, nAlpha, nBeta, bNullMove, nEvalPly);
		fflush(logfile);
	}
#endif

	if ((nSearchNodes & nCheckNodes) == 0)
	{
		if (CheckForInput(FALSE))
			HandleCommand();

		if ((CheckTimeRemaining() == FALSE) && (nEngineCommand != STOP_THINKING))	// a command might tell us to stop
			nEngineCommand = END_THINKING;
	}

	if (nEngineCommand == END_THINKING)	// out of time or "move now"
		return(0);

	if (nEngineCommand == STOP_THINKING)	// just stop thinking and return to the main loop as quickly as possible, losing all search info
		return(0);

	pv.pvLength = 0;

	PosSignature bbSig = bbEvalBoard.signature;

	// check for draw by repetition
	if (nEvalPly && EvalPositionRepeated(bbSig))
	{
#if 0 // FULL_LOG
		int	n;

		if (bLog)
		{
			fprintf(logfile, "Position repeated! ");
			fprintf(logfile, "%08X\n", bbSig);
			for (n = nEvalMove - 1; n >= 0; n--)
				fprintf(logfile, "\t%d = %08X\n", n, cmEvalGameMoveList[n].dwSignature);
		}

#endif
		if (nAlpha >= 0)
			return(nAlpha);
		if (nBeta <= 0)
			return(nBeta);
		return(0);
	}

	// check for draw by 50-move rule
	if (nEvalPly && (bbEvalBoard.fifty >= 100))
	{
		// verify that the last move wasn't checkmate!
		BBGenerateAllMoves(&bbEvalBoard, &cmEvalMoveList[0], &nNumMoves, FALSE);
		if (nNumMoves)
		{
			if (nAlpha >= 0)
				return(nAlpha);
			if (nBeta <= 0)
				return(nBeta);
			return(0);
		}
	}

#if USE_EGTB
	// Probe Gaviota EGTBs
	if (nEvalPly && tb_available && (BitCount(bbEvalBoard.bbOccupancy) <= 5))
	{
		nEval = GaviotaTBProbe(&bbEvalBoard, (nEvalPly >= 3) && (nDepth <= 2));

		if (nEval != EXIT_FAILURE)
		{
			if (nEval > 0)
				nEval -= nEvalPly;
			else if (nEval < 0)
				nEval += nEvalPly;

			if (nEval <= nAlpha)
				return(nAlpha);
			else if (nEval >= nBeta)
				return(nBeta);

			return(nEval);
		}
	}
#endif

	// we've gone to the max search depth, so just evaluate
	if (nEvalPly >= MAX_DEPTH)
		return(BBEvaluate(&bbEvalBoard, nAlpha, nBeta));

#if USE_MATE_DISTANCE_PRUNING
	// mate distance pruning
	int nMateValue = CHECKMATE - nEvalPly;
	if (nMateValue < nBeta)
	{
		nBeta = nMateValue;
		if (nAlpha >= nMateValue)
			return(nAlpha);
	}
	nMateValue = -CHECKMATE + nEvalPly;
	if (nMateValue > nAlpha)
	{
		nAlpha = nMateValue;
		if (nBeta <= nMateValue)
			return(nBeta);
	}
#endif

	BOOL bPVNode = (nBeta - nAlpha) > 1;

#if USE_HASH
	// probe the hash table
	int			nHashFlags = 0;
	BYTE		nHashType = HASH_ALPHA;
	HASH_ENTRY* heHash = ProbeHash(bbSig);

	if ((heHash != NULL) && !bPVNode && (nEngineMode == ENGINE_PONDERING ? nEvalPly >= 3 : nEvalPly >= 2))
	{
#if FULL_LOG
		if (nEvalPly == 0)
		{
			fprintf(logfile, "Got Hash Entry at eval ply %d -- nEval %d, from 0x%02X, to 0x%02X, flags %08X\n",
				nEvalPly, heHash->h.nEval, heHash->h.from, heHash->h.to, heHash->h.nFlags);
			fflush(logfile);
		}
#endif
		nHashFlags = heHash->h.nFlags;

		if (heHash->h.nDepth >= nDepth)
		{
			int nHashEval = heHash->h.nEval;

			if (nHashEval >= (CHECKMATE / 2))
				nHashEval -= nEvalPly;
			else if (nHashEval < -(CHECKMATE / 2))
				nHashEval += nEvalPly;

			if (nHashFlags & HASH_EXACT)
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Exact\n");
					fflush(logfile);
				}
#endif
				if (nHashEval >= nBeta)
					return(nBeta);
				if (nHashEval <= nAlpha)
					return(nAlpha);
				return nHashEval;
			}

			if ((nHashFlags & HASH_ALPHA) && (nHashEval <= nAlpha))
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Alpha\n");
					fflush(logfile);
				}
#endif
				return nAlpha;
			}
			else if ((nHashFlags & HASH_BETA) && (nHashEval >= nBeta))
			{
#if FULL_LOG
				if (bLog)
					nHashReturns++;
				if (nEvalPly == 0)
				{
					fprintf(logfile, "Returning Hash Beta\n");
					fflush(logfile);
				}
#endif
				return nBeta;
			}
		}
	}
#else // USE_HASH
	HASH_ENTRY* heHash = NULL;
#endif

#if USE_SMP
	if (bSlave)
		smSharedMem->sdSlaveData[nSlaveNum].nSearchNodes++;	// only increment search nodes for slaves if there wasn't a hash cutoff
#endif

	// depth is 0, return quiescent search score
	if (nDepth <= 0)
	{
		nQuiesceDepth = 0;

#if 0 // FULL_LOG
		fprintf(logfile, "Going to QSearch\n");
#endif

#if USE_QS_RECAPTURE
		return(BBQuiesce(nAlpha, nBeta, pvLine, NO_SQUARE));
#else
		return(BBQuiesce(nAlpha, nBeta, pvLine));
#endif
	}

	int nStaticEval = -MAX_WINDOW;

#if USE_FUTILITY_PRUNING
	if (!bNullMove && !bPVNode && !bInCheck && (nDepth < 4))
	{
		int nAlphaMargin[4] = { 20000, 150, 275, 325 };
		int nBetaMargin[4] = { 20000, 75, 150, 275 };

		nStaticEval = BBEvaluate(&bbEvalBoard, -MAX_WINDOW, MAX_WINDOW);

		if (nStaticEval <= nAlpha - nAlphaMargin[nDepth])
		{
			nQuiesceDepth = 0;

#if USE_QS_RECAPTURE
			int nScore = BBQuiesce(nAlpha - nAlphaMargin[nDepth], nBeta - nAlphaMargin[nDepth], pvLine, NO_SQUARE);
#else
			int nScore = BBQuiesce(nAlpha - nAlphaMargin[nDepth], nBeta - nAlphaMargin[nDepth], pvLine);
#endif
			if (nScore <= nAlpha - nAlphaMargin[nDepth])
				return(nAlpha);
		}

		if (nStaticEval >= nBeta + nBetaMargin[nDepth])
			return(nBeta); 
	}
#endif

#if USE_NULL_MOVE
	// null move reductions
	do
	{
		CHESSMOVE	cmNull;

		int R = 3 + (nDepth / 6);

		if (
//			nStaticEval >= nBeta || 
			bPVNode ||
			(nEvalPly == 0) ||
			(nDepth <= 1) ||
#if USE_HASH
			(nHashType & HASH_MATE_THREAT) ||
#endif
			!BBIsNullOk() ||
			bNullMove ||
			bInCheck)
			break;

#if 0 // FULL_LOG
		fprintf(logfile, "Doing Null Move\n");
#endif
		cmNull.moveflag = MOVE_NULL;

		BBMakeNullMove(&cmNull, &bbEvalBoard);
		cmEvalGameMoveList[nEvalMove++] = cmNull;
		cmNull.dwSignature = bbEvalBoard.signature;
		nEvalPly++;

		int null_eval = -BBAlphaBeta(nDepth - 1 - R, -nBeta, -nBeta + 1, &pv, TRUE);

#if 0 // FULL_LOG
		fprintf(logfile, "Got Null Eval -- %d\n", null_eval);
#endif
		BBUnMakeNullMove(&cmNull, &bbEvalBoard);
		nEvalMove--;
		nEvalPly--;

		if (null_eval >= nBeta)
		{
#if USE_HASH
			SaveHash(NULL, nDepth, nBeta, HASH_BETA, nEvalPly, bbSig);
#endif
#if 0 // FULL_LOG
			fprintf(logfile, "Returning Null Eval\n");
#endif
			return(nBeta);
		}

#if USE_HASH
		if (null_eval <= -MATE_THREAT)
			bNullMateThreat = TRUE;
#endif
	} while (0);
#endif

	// finally we generate the legal moves for the current position
	BBGenerateAllMoves(&bbEvalBoard, &cmEvalMoveList[0], &nNumMoves, FALSE);

	// there are no legal moves! Is it checkmate or draw?
	if (nNumMoves == 0)
	{
		int nRetval = 0;
		if (bInCheck)
			nRetval = -CHECKMATE + nEvalPly;

		if (nRetval <= nAlpha)
			return(nAlpha);
		else if (nRetval >= nBeta)
			return(nBeta);
		else
			return(nRetval);
	}

	// check for a move from the hash and put it at the front of the move ordering
	BOOL	bFound = FALSE;

#if USE_HASH
	if ((heHash != NULL) && (heHash->h.from != NO_SQUARE))
	{
		for (n = 0; n < nNumMoves; n++)
		{
			if ((cmEvalMoveList[n].fsquare == heHash->h.from) &&
				(cmEvalMoveList[n].tsquare == heHash->h.to)
				//				&&	(cmEvalMoveList[n].moveflag == heHash->h.moveflag)
				)
			{
				// make sure that if it's a promotion, that the piece being promoted to is a match
				if ((cmEvalMoveList[n].moveflag & MOVE_PIECEMASK) == (heHash->h.moveflag & MOVE_PIECEMASK))
				{
					cmEvalMoveList[n].nScore += HASH_SORT_VAL;
					bFound = TRUE;
#if FULL_LOG
					if (nEvalPly == 0)
					{
						fprintf(logfile, "hash move found\n");
						fflush(logfile);
					}
#endif
				}
			}

			if (bFound)
				break;
		}
	}
#else
	// get the best move from the previous depth 
	if (nEvalPly == 0)
	{
//		printf("checking against %d, %d\n", cmChosenMove.fsquare, cmChosenMove.tsquare);
		for (n = 0; n < nNumMoves; n++)
		{
			if ((cmEvalMoveList[n].fsquare == cmChosenMove.fsquare) &&
				(cmEvalMoveList[n].tsquare == cmChosenMove.tsquare)
				&& (cmEvalMoveList[n].moveflag == cmChosenMove.moveflag)
				)
			{
				cmEvalMoveList[n].nScore += HASH_SORT_VAL;
				bFound = TRUE;
//				printf("found best move\n");
			}

			if (bFound)
				break;
		}
	}
#endif

#if FULL_LOG
	if ((nEvalPly == 0) && !bFound)
	{
		fprintf(logfile, "hash move NOT found\n");
		fflush(logfile);
	}
#endif

#if USE_IID
	// internal iterative deepening -- no hash move -- search to a shallower depth (depth / 3) to hopefully find a best move
	if (!bFound && (nDepth >= 5))
	{
		PV	pvIID;
		int nScore;

		nScore = BBAlphaBeta(nDepth / 3, nAlpha, nBeta, &pvIID, FALSE);
#if 1
		if (nScore <= nAlpha)
			nScore = BBAlphaBeta(nDepth / 3, -MAX_WINDOW, MAX_WINDOW, &pvIID, FALSE);
#else
		if (nScore > nAlpha)
#endif
		{
			for (n = 0; n < nNumMoves; n++)
			{
				if ((cmEvalMoveList[n].fsquare == pvIID.pv[0].fsquare) &&
					(cmEvalMoveList[n].tsquare == pvIID.pv[0].tsquare) 
					// && (cmEvalMoveList[n].moveflag == pvIID.pv[0].moveflag)
					)
				{
					// make sure that if it's a promotion, that the piece being promoted to is a match
					if ((cmEvalMoveList[n].moveflag & MOVE_PIECEMASK) == (pvIID.pv[0].moveflag & MOVE_PIECEMASK))
					{
						cmEvalMoveList[n].nScore += HASH_SORT_VAL;
						bFound = TRUE;
					}
				}

				if (bFound)
					break;
			}
		}
	}
#endif

#if USE_IIR
	// Still didn't find a good move, so just reduce
	if ((nEvalPly > 1) && !bFound && !bPVNode && (nDepth >= 4))
		nDepth--;
#endif

	if (nEngineCommand == END_THINKING)	// out of time or "move now"
		return(0);

	if (nEngineCommand == STOP_THINKING)	// just stop thinking and return to the main loop as quickly as possible, losing all search info
		return(0);

	ScoreMoves(&cmEvalMoveList[0], nNumMoves);

#if FULL_LOG
	if (bLog)
	{
		if (nEvalPly == 0)
		{
			fprintf(logfile, "Depth = %d, nAlpha = %d, nBeta = %d\n", nDepth, nAlpha, nBeta);

			for (n = 0; n < nNumMoves; n++)
			{
				fprintf(logfile, "Move %d: %c%c-%c%c = %08X\n", n,
					BBSQ2COLNAME(cmEvalMoveList[n].fsquare), BBSQ2ROWNAME(cmEvalMoveList[n].fsquare),
					BBSQ2COLNAME(cmEvalMoveList[n].tsquare), BBSQ2ROWNAME(cmEvalMoveList[n].tsquare),
					cmEvalMoveList[n].nScore);
			}
			fflush(logfile);
		}
	}
#endif

	cmBestMove.fsquare = NO_SQUARE;

#if USE_IMPROVING
	BOOL bImproving = FALSE;

	if (nStaticEval == -MAX_WINDOW)
		nStaticEval = BBEvaluate(&bbEvalBoard, -MAX_WINDOW, MAX_WINDOW);

	nEvalStack[nEvalPly] = nStaticEval;
	if ((nEvalPly > 2) && (nEvalStack[nEvalPly] > nEvalStack[nEvalPly - 2]))
		bImproving = TRUE;
#endif

#if USE_LMP
	BOOL bUseLMP = (!bInCheck && (nDepth < 4) && !bPVNode && (BitCount(bbEvalBoard.bbOccupancy) > 5));	// don't prune potential winning quiet moves from tablebases
#endif

	// loop through legal moves
	for (n = 0; n < nNumMoves; n++)
	{
		CHESSMOVE	cmMove;

		cmMove = *GetNextMove(&cmEvalMoveList[0], nNumMoves);

#if FULL_LOG
		if (bLog)
		{
			if (nEvalPly == 0)
			{
				char	moveString[16];

				fprintf(logfile, "   ");
				fprintf(logfile, " looking at move %s -- score = %d, alpha = %d, beta = %d\n", MoveToString(moveString, &cmMove, FALSE), cmMove.nScore, nAlpha, nBeta);
				fflush(logfile);
			}
		}
#endif

		// get the SEE value of a capture - used by LMR
		int nSee = 0;
		if (cmMove.moveflag & MOVE_CAPTURE)
			nSee = BBSEEMove(&cmMove, bbEvalBoard.sidetomove);

		BBMakeMove(&cmMove, &bbEvalBoard, TRUE);
		cmMove.dwSignature = bbEvalBoard.signature;	// bbEvalBoard.signature;

		cmEvalGameMoveList[nEvalMove++] = cmMove;
		nEvalPly++;

		nReductions = 0;

		// try some late move reduction conditions
//      int tsquare = cmMove.tsquare;
//      int tpiece = bbEvalBoard.squares[tsquare];

		if ((nEvalPly > 1)		// not at the root
			// && !bPVNode		// not a PV node
			&& !bInCheck		// not in check
			&& (n > 2)			// not one of the first three moves in the movelist
			&& !(cmMove.moveflag & (MOVE_PROMOTED | MOVE_CHECK | MOVE_OOO | MOVE_OO))	// not a promotion, castling or checking move
			&& (!(cmMove.moveflag & MOVE_CAPTURE) || (nSee < 0))    // must be either a bad capture or not a capture
			// && (nAlpha > -MATE_THREAT)	// not in a mate threat against the side to move
			&& (nDepth > 3)			// not at or near the leaves
			&& (cmMove.nScore < KILLER_3_SORT_VAL)  // not a killer move
			// && ((PIECEOF(tpiece) != PAWN) || !IsPassedPawn(&bbEvalBoard, tsquare, (COLOROF(tpiece) == XWHITE ? WHITE : BLACK))) // not a move by a passer
			)
		{
			nReductions = LMRReductions[min(nDepth, 31)][min(n, 31)];   // reduce based on current depth remaining and move number

			if (nReductions && bPVNode)
				nReductions--;
#if USE_IMPROVING
			if (nReductions && bImproving)
				nReductions--;
#endif
//			if (nReductions && (cmMove.nScore >= KILLER_3_SORT_VAL) && !(cmMove.moveflag & MOVE_CAPTURE))  // reduce less for quiet killer moves
//				nReductions--;
//			if (nReductions && (cmMove.moveflag & 0x70)) // (MOVE_PROMOTED | MOVE_OOO | MOVE_OO)
//				nReductions--;
		}

		// try some extension conditions - check or single reply
		if ((cmMove.moveflag & MOVE_CHECK) || (nNumMoves == 1))
			nReductions--;

#if USE_LMP
		if (bUseLMP && (n > (12 + (nDepth * 2))) && !(cmMove.moveflag & MOVE_CHECK) && (nEvalPly > 1) && (nReductions >= 0))
		{
			BBUnMakeMove(&cmMove, &bbEvalBoard, TRUE);
			nEvalMove--;
			nEvalPly--;
			continue;
		}
#endif

		if (nReductions >= nDepth)
			nReductions = (nDepth - 1);

		// PVS
		if (n == 0)
			nEval = -BBAlphaBeta(nDepth - 1 - nReductions, -nBeta, -nAlpha, &pv, FALSE);    // reduced full window for first move in list
		else
		{
			nEval = -BBAlphaBeta(nDepth - 1 - nReductions, -nAlpha - 1, -nAlpha, &pv, FALSE); // reduced null window search for all other moves
#if 1
			if ((nEval > nAlpha) && bPVNode && (nEngineCommand != STOP_THINKING) && (nEngineCommand != END_THINKING))
			{
				//	memset(&pv, 0, sizeof(PV));
				nEval = -BBAlphaBeta(nDepth - 1 - nReductions, -nBeta, -nAlpha, &pv, FALSE);   // reduced full window search if promising
			}
#endif
		}

		// if we did a depth reduction but improved alpha, research at proper depth
		if ((nEval > nAlpha) && (nReductions > 0))
		{
			if ((nEngineCommand != STOP_THINKING) && (nEngineCommand != END_THINKING))
				nEval = -BBAlphaBeta(nDepth - 1, -nBeta, -nAlpha, &pv, FALSE);  // full-depth full window if still promising
		}

		BBUnMakeMove(&cmMove, &bbEvalBoard, TRUE);
		nEvalMove--;
		nEvalPly--;

#if FULL_LOG
		if (bLog && nEvalPly == 0)
		{
			fprintf(logfile, "		eval = %d\n", nEval);
			fflush(logfile);
		}
#endif

		if ((nEngineCommand == END_THINKING) || (nEngineCommand == STOP_THINKING))
			break;

		if ((nEval > nAlpha) || ((nEvalPly == 0) && (n == 0)))
		{
#if FULL_LOG
			if (bLog && nEvalPly == 0)
			{
				fprintf(logfile, "		alpha improved\n");
				fflush(logfile);
			}
#endif

			cmBestMove = cmMove;

#if USE_HISTORY
			if (((cmBestMove.moveflag & MOVE_CAPTURE) == 0) && (nDepth > 1))
				UpdateHistory(&cmBestMove, nDepth);	// update history only for non-capture moves
#endif

			/* update the PV */
			if (pvLine)
			{
				pvLine->pv[0].fsquare = cmMove.fsquare;
				pvLine->pv[0].tsquare = cmMove.tsquare;
				pvLine->pv[0].moveflag = cmMove.moveflag;
				memcpy(&pvLine->pv[1], &pv.pv, pv.pvLength * sizeof(PVMOVE));
				pvLine->pvLength = pv.pvLength + 1;
			}

			if (nEvalPly == 0)
			{
				char	comment;

				bKeepThinking = FALSE;	// flag for not making a move because things are looking worse

				if (nEval <= nAlpha)
				{
					comment = '?';
					bThinkUntilSafe = TRUE;	// fail low = things are looking worse!
				}
				else if (nEval >= nBeta)
				{
					comment = '!';
					if (nEval < MINOR_VAL)
						bKeepThinking = TRUE;	// fail high = make sure before you play it unless we think we're ahead by at least a minor piece
				}
				else
					comment = '\0';
				PrintPV(nEval, bbEvalBoard.sidetomove, comment, FALSE);
			}

#if USE_HASH
			if (nEval > nAlpha)
				nHashType = HASH_EXACT;
#endif

			nAlpha = nEval;

			if (nEval >= nBeta)
			{
#if USE_KILLERS
				if ((cmBestMove.moveflag & (MOVE_CAPTURE | MOVE_PROMOTED)) == 0)
					UpdateKiller(nEvalPly, &cmBestMove, nEval);	// update killers only for non-capture and non-promotion moves
#endif

#if USE_HASH
				SaveHash(&cmBestMove, nDepth, nBeta, HASH_BETA | (bNullMateThreat ? HASH_MATE_THREAT : 0), nEvalPly, bbSig);
#endif

#if FULL_LOG
				if (bLog && nEvalPly == 0)
				{
					fprintf(logfile, "		beta improved\n");
					fflush(logfile);
				}
#endif

				return(nBeta);
			}

			if (nEvalPly == 0)
				nCurEval = nEval;
		}
	}

#if USE_HASH
	if ((nEngineCommand != END_THINKING) && (nEngineCommand != STOP_THINKING))
	{
		// only save to the hash if we had a move that improved alpha
		if (cmBestMove.fsquare != NO_SQUARE)
			SaveHash(&cmBestMove, nDepth, nAlpha, nHashType | (bNullMateThreat ? HASH_MATE_THREAT : 0), nEvalPly, bbSig);
	}
#endif

	return(nAlpha);
}

/*========================================================================
** Think - Start Alpha/Beta search on the current game board up to a given
** depth returning an evaluation and assigning the best move
**========================================================================
*/
int	Think(int nDepth)
{
	int		nEval;

	// prep for search evaluation
	nEvalPly = 0;
	nCurEval = NO_EVAL;
	bKeepThinking = bThinkUntilSafe = FALSE;
#if USE_NULL_MOVE
	bIsNullOk = BBIsNullOk();
#endif

	bbEvalBoard = bbBoard;
	memcpy(cmEvalGameMoveList, cmGameMoveList, sizeof(cmEvalGameMoveList));
	nEvalMove = nGameMove;

	evalPV.pvLength = 0;

#if USE_IMPROVING
	memset(&nEvalStack, -MAX_WINDOW, sizeof(nEvalStack));
#endif

	if (nDepth == 1)
	{
		nPrevEval = NO_EVAL;

		prevDepthPV.pvLength = 0;

#if USE_KILLERS
		ClearKillers(FALSE);
#endif
#if USE_HISTORY
		ClearHistory();
#endif
#if USE_HASH
//      nHashAge++;
//		ClearHash();
#endif
	}
	else
	{
#if USE_KILLERS
		ClearKillers(TRUE);	// only reset the scores on later depths, but leave the killers for move ordering
#endif
	}

#if USE_ASPIRATION
	if ((nDepth == 1) /* || ((BitCount(bbEvalBoard.bbOccupancy) <= 5) && tb_available) */)
	{
#if FULL_LOG
		if (bLog)
		{
			fprintf(logfile, "\n\nThink called with depth %d, alpha %d, beta %d\n", nDepth, -MAX_WINDOW, MAX_WINDOW);
			fflush(logfile);
		}
#endif

		nEval = BBAlphaBeta(nDepth, -MAX_WINDOW, MAX_WINDOW, &evalPV, FALSE);
	}
	else
	{
		int		nHighWindow, nLowWindow;
		int		nNumSearches = 0;
		BOOL	bKeepSearching;

		nHighWindow = nPrevEval + ASPIRATION_WINDOW;
		nLowWindow = nPrevEval - ASPIRATION_WINDOW;

#if FULL_LOG
		if (bLog)
		{
			fprintf(logfile, "\n\nThink called with depth %d, alpha %d, beta %d\n", nDepth, nLowWindow, nHighWindow);
			fflush(logfile);
		}
#endif

		do
		{
			bKeepSearching = FALSE;
			nNumSearches++;

			if (nNumSearches >= MAX_ASPIRATION_SEARCHES)
			{
				nLowWindow = -MAX_WINDOW;
				nHighWindow = MAX_WINDOW;
			}

			nEval = BBAlphaBeta(nDepth, nLowWindow, nHighWindow, &evalPV, FALSE);

			if ((nEngineCommand != STOP_THINKING) && (nEngineCommand != END_THINKING) &&
				((nEval <= nLowWindow) || (nEval >= nHighWindow)))
			{
				int nDiff = nNumSearches * ASPIRATION_WINDOW;

				prevDepthPV = evalPV;
				evalPV.pvLength = 0;
				bKeepSearching = TRUE;

				if (nEval <= nLowWindow)
					nLowWindow -= nDiff;
				else
					nHighWindow += nDiff;

				if (nLowWindow < -MAX_WINDOW)
					nLowWindow = -MAX_WINDOW;
				else if (nHighWindow > MAX_WINDOW)
					nHighWindow = MAX_WINDOW;
			}
		} while (bKeepSearching);
	}
#else	// USE_ASPIRATION

	nEval = BBAlphaBeta(nDepth, -MAX_WINDOW, MAX_WINDOW, &evalPV, FALSE);

#endif	// USE_ASPIRATION

	if (nEngineCommand == STOP_THINKING)
		return(0);

	if (evalPV.pvLength && (nEval != MAX_WINDOW) && (nEval != -MAX_WINDOW))	// we have a move
	{
		nPrevEval = nEval;
		cmChosenMove.fsquare = evalPV.pv[0].fsquare;
		cmChosenMove.tsquare = evalPV.pv[0].tsquare;
		cmChosenMove.moveflag = evalPV.pv[0].moveflag;
		// printf("best move = %d, %d, %d\n", cmChosenMove.fsquare, cmChosenMove.tsquare, cmChosenMove.moveflag);
		prevDepthPV = evalPV;
	}
	else	// we had to stop the search due to time, so we don't have a move at this depth, so use last depth's move
	{
		nEval = nPrevEval;
		cmChosenMove.fsquare = prevDepthPV.pv[0].fsquare;
		cmChosenMove.tsquare = prevDepthPV.pv[0].tsquare;
		cmChosenMove.moveflag = prevDepthPV.pv[0].moveflag;
		evalPV = prevDepthPV;
	}

	return(nEval);
}

void InitThink(void)
{
	int d, m, red;

	for (d = 1; d < 32; d++)        // remaining depth
	{
		for (m = 1; m < 32; m++)    // move number
		{
			// listed in order of aggressiveness (least to most)
			// red = (int)(1.2 + (log(d) * log(m)) / 4.0);		// courtesy Blocky
			// red = (int)(0.38 + log(d) * log(m) / 2.17);		// courtesy Publius
			// red = (int)(0.32 + (log(d) * log(m)) / 2.24);	// courtesy Nawito
			red = (int)(0.5 + (log(d) * log(m)) / 2.5);		// my own guesstimate
			// red = (int)(1 + (log(d) * log(m)) / 2.0);		// courtesy Supernova
			// red = (int)((0.81 * log(d)) + (1.08 * log(m)));	// courtesy Dumb

			LMRReductions[d][m] = min(d, red);
//			fprintf(logfile, "%2d-%2d=%d,", d, m, LMRReductions[d][m]);
		}
//		fprintf(logfile, "\n");
	}
}
