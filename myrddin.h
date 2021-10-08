/*
Myrddin XBoard / WinBoard compatible chess engine written in C
Copyright(C) 2021  John Merlino

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

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>

#ifdef _MSC_VER
// --- eliminate some MS VC++ compiler warning messages
// warning C4127: conditional expression is constant (ie do-while(0))
#pragma warning (disable: 4127)
// warning C4996: deprecated C library function
#pragma warning (disable: 4996)
//  #define _CRT_SECURE_NO_DEPRECATE
//  #define _CRT_NONSTDC_NO_DEPRECATE
#endif

#define INDEX_CHECK(index, array)  assert((index) >= 0 &&  (index) < sizeof array / sizeof array[0]);

#define FALSE	0
#define TRUE	1

#define USE_HASH			TRUE
#if USE_HASH
#define USE_PAWN_HASH		TRUE
#define USE_EVAL_HASH		TRUE
#endif

#define USE_PVS			    TRUE
#define USE_IID				TRUE	
#define USE_IIR             FALSE   
#define USE_ASPIRATION		TRUE

#define USE_HISTORY			TRUE
#define USE_KILLERS			TRUE

#define USE_NULL_MOVE		TRUE
#if USE_NULL_MOVE
#define USE_NULL_VERIFY		FALSE	// not helpful?
#endif

#define USE_LMR				TRUE
#if USE_LMR
#define USE_AGGRESSIVE_LMR	TRUE
#endif
#define USE_LMP				FALSE	// worse?
#define USE_EXTENSIONS		TRUE
#define USE_PRUNING			TRUE

#define USE_SEE				TRUE
#define USE_SEE_MOVE_ORDER	FALSE	// not helpful

#define USE_SMP				TRUE
#if USE_SMP
#define DEBUG_SMP			FALSE	// debugging only!
#endif

#ifdef _DEBUG
#ifdef WIN64
#define USE_EGTB			TRUE
#else
#define USE_EGTB			FALSE
#endif
#else
#define USE_EGTB			TRUE
#endif

#define VERIFY_BOARD		FALSE

#ifndef _MSC_VER
#define max(a,b) ((a) < (b) ? (b) : (a))
#define min(a,b) ((a) > (b) ? (b) : (a))
#endif

#define KING_AND_MINOR_DRAW TRUE	// change this to false if testing studies in which the side with only a minor can mate

#define MAX_DEPTH			(128)
#define MAX_EXT_DEPTH		(10)	// how much has this line been extended past original search depth, remembering that reductions take away from the extension depth
#define MAX_QUIESCE_DEPTH	(MAX_DEPTH)
#define MAX_REDUCTIONS		((nEvalPly / 3)	+ 1) // max reductions that can be applied to any branch
#define MAX_NULL_MOVES		(10)
#define MAX_KILLERS			(2)

#define INFINITY			0x8000
#define CHECKMATE			0x7FFF
#define NO_EVAL				0xDEAD

#define MAX_ASPIRATION_SEARCHES	11	// should ideally be an odd number, don't ask why

#if (MAX_ASPIRATION_SEARCHES <= 3)
#define ASPIRATION_WINDOW	40
#define ASPIRATION_WINDOW_2	110
#elif (MAX_ASPIRATION_SEARCHES <= 9)
#define ASPIRATION_WINDOW	25
#define ASPIRATION_WINDOW_2	50
#else
#define ASPIRATION_WINDOW	16
#define ASPIRATION_WINDOW_2	32
#endif

#define NCOLORS	2
#define NPIECES	6

#define CLOCK_TO_USE		40		// divide remaining clock by this number to get base thinking time
#define PANIC_CLOCK_TO_USE	40		// divide remaining clock by this number when we have less than PANIC_THRESHHOLD time remaining
#define PANIC_THRESHHOLD	10000	// ten seconds left!

#define NUM_RESIGN_MOVES	4		// number of moves above resign threshold before resigning

////////////////////////////////////////////////////////////////////////////////
typedef unsigned char   ColorType;
typedef unsigned char   PieceType;
typedef unsigned char   SquareType;
typedef unsigned short  MoveFlagType;

typedef unsigned long long	PosSignature;

////////////////////////////////////////////////////////////////////////////////
//  The bits in the Piece char are as follows:
//      xxBWxPPP
//      B       piece is XBLACK
//      W       piece is XWHITE
//      PPP     piece is one of { KING, QUEEN, ROOK, BISHOP, KNIGHT, PAWN }
#define EMPTY               0x00
#define XWHITE              0x10
#define XBLACK              0x20

#define KING                0x00
#define QUEEN               0x01
#define ROOK                0x02
#define BISHOP              0x03
#define KNIGHT              0x04
#define PAWN                0x05

#define COLOROF(piece)      ((PieceType)((piece) & 0x30))	// XBLACK | XWHITE
#define PIECEOF(piece)      ((PieceType)((piece) & 0x07))
#define OPPOSITE(color)     ((PieceType)((color) ^ 0x30))	// for XWHITE and XBLACK flipping

#define KING_VAL	10000	// used for SEE calculation only
#define QUEEN_VAL	950
#define ROOK_VAL	500
#define MINOR_VAL	320
#define PAWN_VAL	100

#define TOTAL_STARTING_WOOD	(QUEEN_VAL + (ROOK_VAL * 2) + (MINOR_VAL * 4) + (PAWN_VAL * 8))

#define WHITE_PAWN		(0x15)
#define WHITE_KNIGHT	(0x14)
#define WHITE_BISHOP	(0x13)
#define WHITE_ROOK		(0x12)
#define WHITE_QUEEN		(0x11)
#define WHITE_KING		(0x10)
#define BLACK_PAWN		(0x25)
#define BLACK_KNIGHT	(0x24)
#define BLACK_BISHOP	(0x23)
#define BLACK_ROOK		(0x22)
#define BLACK_QUEEN		(0x21)
#define BLACK_KING		(0x20)

#define  WHITE_KINGSIDE_BIT   (0x01)
#define  WHITE_QUEENSIDE_BIT  (0x02)
#define  BLACK_KINGSIDE_BIT   (0x04)
#define  BLACK_QUEENSIDE_BIT  (0x08)

#define  NO_EN_PASSANT        (0xFF)
#define  NO_SQUARE            (0xFF)

#define  BSIZE                  (8)
#define  MAX_MOVE_LIST          (1024)
#define  MAX_LEGAL_MOVES        (219)	// the actual known number for a constructed position is 218
#define  MOVESTRLEN             (20)

#define	 FUTILITY_MARGIN	(MINOR_VAL)
#define	 R_FUTILITY_MARGIN	(ROOK_VAL)
#define  Q_FUTILITY_MARGIN	(QUEEN_VAL)

// these are for two-dimensional arrays where the first dimension is the color
#define	WHITE			(0)
#define BLACK           (1)
#define	OPPONENT(color)	(1 - color)	// for WHITE and BLACK flipping
#define NO_SIDE			(0xF)

////////////////////////////////////////////////////////////////////////////////
// a chess move
typedef struct UNDOMOVE
{
    PosSignature	dwSignature;
#if USE_PAWN_HASH
    PosSignature	dwPawnSignature;
#endif
    BYTE			castle_status;
    SquareType		en_passant_pawn;
    BYTE			in_check_status;
    BYTE			fifty_move;
    SquareType		capture_square;
    PieceType		captured_piece;
} UNDOMOVE, *PUNDOMOVE;

typedef struct CHESSMOVE
{
    PosSignature	dwSignature;
    long 			nScore;
    MoveFlagType	moveflag;
    SquareType		fsquare;
    SquareType		tsquare;
    UNDOMOVE		save_undo;
} CHESSMOVE;

typedef struct PV
{
    long		pvLength;
    CHESSMOVE	pv[MAX_DEPTH];
} PV;

extern BOOL			bSlave;
extern int			nSlaveNum;

#if USE_SMP
#define MAX_CPUS			16
#define NUM_SLAVE_STRINGS	4

typedef struct SLAVE_DATA
{
    char		szEngineCommand[NUM_SLAVE_STRINGS][256];
	int			nNextToSend;
	int			nNextToReceive;
    int			nSearchNodes;
	BOOL		bLocked;
} SLAVE_DATA, *PSLAVE_DATA;

typedef struct SHARED_MEM
{
	SLAVE_DATA	sdSlaveData[MAX_CPUS - 1];
} SHARED_MEM, *PSHARED_MEM;

typedef struct SHARED_HASH
{
    PVOID HashTable;
    PVOID EvalHashTable;
    PVOID PawnHashTable;
} SHARED_HASH, *PSHARED_HASH;

extern int			nCPUs;
extern HANDLE		hSharedMem, hSharedHash;
extern char			szSharedMemName[32];
extern char			szSharedHashName[32];
extern SHARED_MEM	*smSharedMem;
extern SHARED_HASH  *shSharedHash;

void KillProcesses(void);
void StartProcesses(int nCPUs);
void SendSlavesString(char *szString);
#endif	// USE_SMP

extern CHESSMOVE	cmGameMoveList[MAX_MOVE_LIST];

extern int		nEGTBCompressionType;
extern char     szEGTBPath[MAX_PATH];

extern PosSignature	dwInitialPosSignature;
extern BOOL			bLog, bEval, bExactThinkTime, bExactThinkDepth, bTuning;
extern int			nEngineMode, nEngineCommand;
extern unsigned int	nGameMove, nFifty;
extern int			nCompSide;
extern int			nThinkDepth;
extern unsigned int	nFischerInc, nMovesBeforeControl;
extern unsigned int nThinkTime, nPonderTime;
extern clock_t      nThinkStart;
extern int			nClockRemaining;
extern unsigned int nCheckNodes;	// number of nodes to search before checking time management
extern CHESSMOVE	cmBestMove, cmPonderMove;
extern FILE		   *logfile;

BOOL	CheckForInput(BOOL bWaitForInput);
void 	HandleCommand(void);

void	PrintPV(int nPVEval, int nSideToMove, char *comment, BOOL bPrintKibitz);
char   *MoveToString(char *moveString, CHESSMOVE *move, BOOL bAddMove);
