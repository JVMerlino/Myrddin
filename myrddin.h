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
#define USE_HASH_IN_QS		FALSE
#define USE_EVAL_HASH		TRUE
#endif

#define USE_ASPIRATION		TRUE
#define MAX_ASPIRATION_SEARCHES	3
#define ASPIRATION_WINDOW	16

#define USE_IID				FALSE
#define USE_IIR             TRUE
#define USE_IMPROVING		FALSE

#define USE_HISTORY			TRUE
#define USE_KILLERS			TRUE
#define MAX_KILLERS			2

#define USE_NULL_MOVE		TRUE

#define USE_FUTILITY_PRUNING	TRUE
#define USE_MATE_DISTANCE_PRUNING   TRUE
#define USE_LMP				TRUE

#define USE_SEE				TRUE	
#define USE_SEE_MOVE_ORDER	FALSE	// not helpful

#define USE_INCREMENTAL_ACC_UPDATE TRUE

#define USE_CEREBRUM_1_0	FALSE	

#define TIME_BANK			500	// milliseconds clock to keep as a buffer
#ifdef _DEBUG
#define VERIFY_BOARD		TRUE
#define assert(x)                // uncomment this if profiling!
#else
#define VERIFY_BOARD        FALSE
#endif

#define USE_OPENING_BOOK	TRUE

#define DO_SELF_PLAY		FALSE	

#if DO_SELF_PLAY
#define SELF_PLAY_RANDOM_MOVES	8
#define USE_EGTB			FALSE
#define USE_SMP				FALSE
#else
#define USE_EGTB			TRUE
#define USE_SMP				FALSE
#endif

#if USE_SMP
#define DEBUG_SMP			FALSE	// adds lots of logging!
#endif

#define SHOW_QS_NODES       FALSE

#ifndef _MSC_VER
#define max(a,b) ((a) < (b) ? (b) : (a))
#define min(a,b) ((a) > (b) ? (b) : (a))
#endif

#define MAX_DEPTH	128

#define MAX_WINDOW	0x8000
#define CHECKMATE	0x7FFF
#define NO_EVAL		0xDEAD
#define MATE_THREAT	0x4000

#define NCOLORS	2
#define NPIECES	6

#define CLOCK_TO_USE		40		// divide remaining clock by this number to get base thinking time
#define PANIC_CLOCK_TO_USE	40		// divide remaining clock by this number when we have less than PANIC_THRESHHOLD time remaining
#define PANIC_THRESHHOLD	5000	// two seconds left!

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

// these are for two-dimensional arrays where the first dimension is the color
#define	WHITE			(0)
#define BLACK           (1)
#define	OPPONENT(color)	(1 - color)	// for WHITE and BLACK flipping
#define NO_SIDE			(0xF)

////////////////////////////////////////////////////////////////////////////////
// a chess move
typedef struct
{
    PosSignature	dwSignature;
    BYTE			castle_status;
    SquareType		en_passant_pawn;
    BYTE			in_check_status;
    BYTE			fifty_move;
    SquareType		capture_square;
    PieceType		captured_piece;
} UNDOMOVE, *PUNDOMOVE;

typedef struct
{
    PosSignature	dwSignature;
    int 			nScore;	// for move ordering
    MoveFlagType	moveflag;
    SquareType		fsquare;
    SquareType		tsquare;
    UNDOMOVE		save_undo;
} CHESSMOVE;

typedef struct
{
	MoveFlagType	moveflag;
	SquareType		fsquare;
	SquareType		tsquare;
} PVMOVE;

typedef struct
{
    int			pvLength;
    PVMOVE		pv[MAX_DEPTH+10];
} PV;

extern BOOL			bSlave;
extern int			nSlaveNum;

#if USE_SMP
#define MAX_CPUS			32
#define NUM_SLAVE_STRINGS	8

typedef struct
{
    char		szEngineCommand[NUM_SLAVE_STRINGS][256];
	int			nNextToSend;
	int			nNextToReceive;
    int			nSearchNodes;
	BOOL		bLocked;
} SLAVE_DATA, *PSLAVE_DATA;

typedef struct
{
	SLAVE_DATA	sdSlaveData[MAX_CPUS - 1];
} SHARED_MEM, *PSHARED_MEM;

typedef struct
{
    PVOID HashTable;
    PVOID EvalHashTable;
} SHARED_HASH, *PSHARED_HASH;

extern int			nCPUs;
extern HANDLE		hSharedMem, hSharedHash;
extern char			szSharedMemName[128];
extern char			szSharedHashName[128];
extern SHARED_MEM	*smSharedMem;
extern SHARED_HASH  *shSharedHash;

void KillProcesses(void);
void StartProcesses(int nCPUs);
void SendSlavesString(char *szString);
#endif	// USE_SMP

extern CHESSMOVE	cmGameMoveList[MAX_MOVE_LIST];

#define USE_BULK_COUNTING   TRUE    // for perft
typedef struct
{
	char		fen[120];
	int			depth;
	unsigned long long	value;
} PERFT_TEST;

#define NUM_PERFT_TESTS	12
extern PERFT_TEST	perft_tests[NUM_PERFT_TESTS];

extern int		nEGTBCompressionType;
extern char     szEGTBPath[MAX_PATH];

extern PosSignature	dwInitialPosSignature;
extern BOOL			bLog, bExactThinkTime, bExactThinkDepth, bExactThinkNodes;
extern int			nEngineMode, nEngineCommand;
extern unsigned int	nGameMove, nFifty;
extern int			nCompSide;
extern int			nThinkDepth;
extern unsigned int	nFischerInc, nMovesBeforeControl;
extern unsigned int nThinkTime, nPonderTime;
extern ULONGLONG    nThinkStart;
extern int			nClockRemaining;
extern unsigned int nCheckNodes, nThinkNodes;	// number of nodes to search before checking time management
extern CHESSMOVE	cmChosenMove, cmPonderMove;
extern FILE		   *logfile;

BOOL	CheckForInput(BOOL bWaitForInput);
void 	HandleCommand(void);

void	PrintPV(int nPVEval, int nSideToMove, char comment, BOOL bPrintKibitz);
char   *MoveToString(char *moveString, CHESSMOVE *move, BOOL bAddMove);
char   *PVMoveToString(char* moveString, PVMOVE* move, BOOL bAddMove);
