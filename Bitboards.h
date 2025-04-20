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

#ifdef _MSC_VER
#pragma warning( disable : 4146)
typedef unsigned __int64 Bitboard;
#else
typedef uint64_t Bitboard;
#endif

#if defined(USE_SSE3) || defined(USE_SSE4)
#include <immintrin.h>

#elif defined(__arm__) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__linux__)
#include <xmmintrin.h>
#define _tzcnt_u64(x) __builtin_ctzll(x)
#else
#include <intrin.h>
#endif

#if USE_CEREBRUM_1_0
#include "cerebrum 1-0.h"
#else
#include "cerebrum 1-1.h"
#endif

enum SQUARES {
    BB_A8 = 0, BB_B8, BB_C8, BB_D8, BB_E8, BB_F8, BB_G8, BB_H8,
    BB_A7, BB_B7, BB_C7, BB_D7, BB_E7, BB_F7, BB_G7, BB_H7,
    BB_A6, BB_B6, BB_C6, BB_D6, BB_E6, BB_F6, BB_G6, BB_H6,
    BB_A5, BB_B5, BB_C5, BB_D5, BB_E5, BB_F5, BB_G5, BB_H5,
    BB_A4, BB_B4, BB_C4, BB_D4, BB_E4, BB_F4, BB_G4, BB_H4,
    BB_A3, BB_B3, BB_C3, BB_D3, BB_E3, BB_F3, BB_G3, BB_H3,
    BB_A2, BB_B2, BB_C2, BB_D2, BB_E2, BB_F2, BB_G2, BB_H2,
    BB_A1, BB_B1, BB_C1, BB_D1, BB_E1, BB_F1, BB_G1, BB_H1,
};

// extern int VFlipSquare[];

#define BB_RANK_8       0x00000000000000FFULL
#define BB_RANK_7       0x000000000000FF00ULL
#define BB_RANK_6       0x0000000000FF0000ULL
#define BB_RANK_5       0x00000000FF000000ULL
#define BB_RANK_4       0x000000FF00000000ULL
#define BB_RANK_3       0x0000FF0000000000ULL
#define BB_RANK_2       0x00FF000000000000ULL
#define BB_RANK_1       0xFF00000000000000ULL

#define BB_FILE_A       0x0101010101010101ULL
#define BB_FILE_B       0x0202020202020202ULL
#define BB_FILE_C       0x0404040404040404ULL
#define BB_FILE_D       0x0808080808080808ULL
#define BB_FILE_E       0x1010101010101010ULL
#define BB_FILE_F       0x2020202020202020ULL
#define BB_FILE_G       0x4040404040404040ULL
#define BB_FILE_H       0x8080808080808080ULL

#define BB_EMPTY		0x0000000000000000ULL

enum files {FILE_A = 0, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H};
enum ranks {RANK_8 = 0, RANK_7, RANK_6, RANK_5, RANK_4, RANK_3, RANK_2, RANK_1};

#define BB_WHITE_SQ     0xAA55AA55AA55AA55ULL
#define BB_BLACK_SQ     0x55AA55AA55AA55AAULL

#define IS_SQ_OK(sq)		assert(sq >= 0 && sq <= 63)
#define IS_PIECE_OK(piece)	assert(piece >= KING && piece <= PAWN)
#define IS_COLOR_OK(color)  assert(color == 1 || color == 0)

extern Bitboard Bit[64];
extern Bitboard wkc, wqc, bkc, bqc;
extern BOOL		bPopcnt;

// common inline functions
__inline DWORD BitScan(Bitboard bb)
{
    assert(bb);
    return(DWORD)(_tzcnt_u64(bb));
}

__inline int File(int sq)  // return FILE_A -> FILE_H
{
	IS_SQ_OK(sq);
    return ((sq) & 7);
}

__inline int Rank(int sq)  // NOTE: ranks are backwards
{
	IS_SQ_OK(sq);
    return ((sq) >> 3);    // same as (sq / 8)
}

#define BBSQ2ROWNAME(sq)		((char)('8' - Rank(sq)))
#define BBSQ2COLNAME(sq)		((char)('a' + File(sq)))
#define BBRC2SQUARE(row, col)	((row * 8) + col)

#if 0
__inline int SqColor(int sq)  // return DARK (0) or LIGHT (1)
{
	IS_SQ_OK(sq);
    return (int)((0xAA55AA55AA55AA55ull >> (sq)) & 1);
}

__inline Bitboard SqToBB(int sq)  // return set bit corresponding with sq
{
	IS_SQ_OK(sq);
    return ((Bitboard)1 << (sq));
}
#endif

__inline int SqNameToSq(char* sqName)
{
    return(('8' - *(sqName + 1)) * 8) + (*sqName - 'a');
}

__inline void SetBit(Bitboard *bb, int sq)
{
	IS_SQ_OK(sq);
    *bb |= Bit[sq];    // set bit corresponding to 'sq'
}

__inline void ClearBit(Bitboard *bb, int sq)
{
	IS_SQ_OK(sq);
    *bb &= ~Bit[sq];    // clear bit corresponding to 'sq'
}

__inline Bitboard GetLSB(Bitboard bb)
{
	assert(bb);
    return bb & -bb;
}

__inline Bitboard PopLSB(Bitboard *bb)
{
	assert(bb);
    Bitboard lsb = *bb & -(*bb);
	assert(lsb);

    *bb ^= lsb;  // remove least significant bit from bb
    return lsb;
}

__inline int BitCount(Bitboard b)
{
#ifdef _WIN64
#ifdef MINGW
    if (bPopcnt)
        return((int)__builtin_popcountll(b));
#else
    if (bPopcnt)
        return((int)__popcnt64(b));
#endif
    else
    {
        b -= ((b>>1) & 0x5555555555555555ULL);
        b = ((b>>2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
        b = ((b>>4) + b) & 0x0F0F0F0F0F0F0F0FULL;
        b *= 0x0101010101010101ULL;
        return b >> 56;
    }
#else
    b -= ((b>>1) & 0x5555555555555555ULL);
    b = ((b>>2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
    b = ((b>>4) + b) & 0x0F0F0F0F0F0F0F0FULL;
    b *= 0x0101010101010101ULL;
    return b >> 56;
#endif
}

#if 0
__inline Bitboard GetMSB(Bitboard bb)
{
    Bitboard	x = bb;

    x |= x >> 32;
    x |= x >> 16;
    x |= x >>  8;
    x |= x >>  4;
    x |= x >>  2;
    x |= x >>  1;
    return((x >> 1) + 1);
}
#endif

typedef struct 
{
	NN_Accumulator Accumulator;
    Bitboard	bbPieces[6][2];
    Bitboard	bbMaterial[2];
    Bitboard	bbOccupancy;
    PosSignature	signature;
    int		squares[64];	// uses piece representation from 0x88 implementation (XWHITE or XBLACK)
	int		epSquare;
	int 	castles;
	int 	fifty;
	int 	sidetomove;
	BOOL	inCheck;
} BB_BOARD;

extern BB_BOARD	bbBoard;

extern Bitboard bbPawnMoves[2][64];
extern Bitboard bbPawnAttacks[2][64];
extern Bitboard bbKnightMoves[64];
extern Bitboard bbKingMoves[64];
extern Bitboard bbPassedPawnMask[2][64];
extern const Bitboard RankMask[8], FileMask[8];

// extern Bitboard bbDiagonalMoves[64];
// extern Bitboard bbStraightMoves[64];
// extern Bitboard bbBetween[8][8];

void RemovePiece(BB_BOARD *Board, int square, BOOL bUpdateNN);
void PutPiece(BB_BOARD *Board, int piece, int square, BOOL bUpdateNN);
void MovePiece(BB_BOARD* Board, int from, int to, BOOL bUpdateNN);
DWORD BitScan(Bitboard b);
void initbitboards(void);
void bbNewGame(BB_BOARD *bbBoard);
int  BitCount(Bitboard b);
