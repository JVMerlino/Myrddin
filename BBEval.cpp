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
#include <windef.h>
#include <math.h>
#include "Myrddin.h"
#include "Bitboards.h"
#include "magicmoves.h"
#include "Think.h"
#include "Hash.h"
#include "Eval.h"
#include "TBProbe.h"
#include "MoveGen.h"
#include "FEN.h"
#include "PArray.inc"

int mg;

// pawn vals
#define DEBUG_PAWNS			FALSE

#define DOUBLED_PAWN_MG		8	// penalty for doubled pawns
#define DOUBLED_PAWN_EG		19
#define BLOCKED_DOT_PAWN_MG 2   // penalty for doubled/tripled pawns that are also blocked
#define BLOCKED_DOT_PAWN_EG 2
#define ISOLATED_PAWN_MG	11  // penalty for isolated pawn
#define ISOLATED_PAWN_EG	4
#define PROTECTED_PASSER_MG 5   // bonus for protected passer
#define PROTECTED_PASSER_EG 11
#define CONNECTED_PASSER_MG 0   // bonus for connected passer
#define CONNECTED_PASSER_EG 4
#define ROOK_BEHIND_PASSER_MG  0  // bonus for rook behind passer
#define ROOK_BEHIND_PASSER_EG  0  

int	nPasserBonusMG[NCOLORS][BSIZE] =
{
    {0, 0, 7, 17, 0, 0, 0, 0},
    {0, 0, 0, 0, 17, 7, 0, 0}
};

int	nPasserBonusEG[NCOLORS][BSIZE] =
{
    {0, 0, 32, 31, 17, 0, 0, 0},
    {0, 0, 0, 17, 31, 32, 0, 0}
};

int nPasserKingDist[7] = { 0, 0, 3, 7, 13, 21, 31 };    // bonus/penalty for passer distance from own/enemy king - only applied to EG

// bishop vals
#define BISHOP_PAIR_MG		23  // bonus
#define BISHOP_PAIR_EG		50	// bonus

// rook vals
enum { NOT_OPEN, OPEN_FILE, SEMIOPEN_FILE };
#define OPEN_FILE_MG		29  // rook on open file bonus - only applied to MG

// king safety
#define F3_PAWN_SHIELD		8   // penalty
#define F6_PAWN_SHIELD		F3_PAWN_SHIELD
#define C3_PAWN_SHIELD		8  // penalty
#define C6_PAWN_SHIELD		C3_PAWN_SHIELD
#define NO_PAWN_SHIELD		16  // penalty for no pawn in front of king
#define KING_ATTACKER		1	// value of each attack on square bordering king (doubled for king square)

int	BBKingMGTable[64] =
{
    -11, 70,  55,  31, -37, -16,  22,  22,
     37, 24,  25,  36,  16,   8, -12, -31,
     33, 26,  42,  11,  11,  40,  35,  -2,
      0, -9,   1, -21, -20, -22, -15, -60,
    -25, 16, -27, -67, -81, -58, -40, -62,
      7, -2, -37, -77, -79, -60, -23, -26,
     12, 15, -13, -72, -56, -28,  15,  17,
     -6, 44,  29, -58,  8,  -25,  34,  28,
};

int BBKingEGTable[64] =
{
    -74, -43, -23, -25, -11, 10,   1, -12,
    -18,   6,   4,   9,   7, 26,  14,   8,
     -3,   6,  10,   6,   8, 24,  27,   3,
    -16,   8,  13,  20,  14, 19,  10,  -3,
    -25, -14,  13,  20,  24, 15,   1, -15,
    -27, -10,   9,  20,  23, 14,   2, -12,
    -32, -17,   4,  14,  15,  5, -10, -22,
    -55, -40, -23,  -6, -20, -8, -28, -47,
};

int	BBQueenMGTable[64] =
{
    865, 902, 922, 911, 964, 948, 933, 928,
    886, 865, 903, 921, 888, 951, 923, 940,
    902, 901, 907, 919, 936, 978, 965, 966,
    881, 885, 897, 894, 898, 929, 906, 915,
    907, 884, 899, 896, 904, 906, 912, 911,
    895, 916, 900, 902, 904, 912, 924, 917,
    874, 899, 918, 908, 915, 924, 911, 906,
    906, 899, 906, 918, 898, 890, 878, 858,
};

int	BBQueenEGTable[64] =
{
    918, 937, 943, 945, 934, 926, 924, 942,
    907, 945, 946, 951, 982, 933, 928, 912,
    896, 921, 926, 967, 963, 937, 924, 915,
    926, 944, 939, 962, 983, 957, 981, 950,
    893, 949, 942, 970, 952, 956, 953, 936,
    911, 892, 933, 928, 934, 942, 934, 924,
    907, 898, 883, 903, 903, 893, 886, 888,
    886, 887, 890, 872, 916, 890, 906, 879,
};

int	BBRookMGTable[64] =
{
    493, 511, 487, 515, 514, 483, 485, 495,
    493, 498, 529, 534, 546, 544, 483, 508,
    465, 490, 499, 497, 483, 519, 531, 480,
    448, 464, 476, 495, 484, 506, 467, 455,
    442, 451, 468, 470, 476, 472, 498, 454,
    441, 461, 468, 465, 478, 481, 478, 452,
    443, 472, 467, 476, 483, 500, 487, 423,
    459, 463, 470, 479, 480, 480, 446, 458,
};

int	BBRookEGTable[64] =
{
    506, 500, 508, 502, 504, 507, 505, 503,
    505, 506, 502, 502, 491, 497, 506, 501,
    504, 503, 499, 500, 500, 495, 496, 496,
    503, 502, 510, 500, 502, 504, 500, 505,
    505, 509, 509, 506, 504, 503, 496, 495,
    500, 503, 500, 505, 498, 498, 499, 489,
    496, 495, 502, 505, 498, 498, 491, 499,
    492, 497, 498, 496, 493, 493, 497, 480,
};

int	BBBishopMGTable[64] =
{
    292, 338, 254, 283, 299, 294, 337, 323,
    316, 342, 319, 319, 360, 385, 343, 295,
    342, 377, 373, 374, 368, 392, 385, 363,
    332, 338, 356, 384, 370, 380, 337, 341,
    327, 354, 353, 366, 373, 346, 345, 341,
    335, 350, 351, 347, 352, 361, 350, 344,
    333, 354, 354, 339, 344, 353, 367, 333,
    309, 341, 342, 325, 334, 332, 302, 313,
};

int	BBBishopEGTable[64] =
{
    288, 278, 287, 292, 293, 290, 287, 277,
    289, 294, 301, 288, 296, 289, 294, 281,
    292, 289, 296, 292, 296, 300, 296, 293,
    293, 302, 305, 305, 306, 302, 296, 297,
    289, 293, 304, 308, 298, 301, 291, 288,
    285, 294, 304, 303, 306, 294, 290, 280,
    285, 284, 291, 299, 300, 290, 284, 271,
    277, 292, 286, 295, 294, 288, 290, 285,
};

int	BBKnightMGTable[64] =
{
    116, 228, 271, 270, 338, 213, 278, 191,
    225, 247, 353, 331, 321, 360, 300, 281,
    258, 354, 343, 362, 389, 428, 375, 347,
    300, 332, 325, 360, 349, 379, 339, 333,
    298, 322, 325, 321, 337, 332, 332, 303,
    287, 297, 316, 319, 327, 320, 327, 294,
    276, 259, 300, 304, 308, 322, 296, 292,
    208, 290, 257, 274, 296, 284, 293, 284,
};

int	BBKnightEGTable[64] =
{
    229, 236, 269, 250, 257, 249, 219, 188,
    252, 274, 263, 281, 273, 258, 260, 229,
    253, 264, 290, 289, 278, 275, 263, 243,
    267, 280, 299, 301, 299, 293, 285, 264,
    263, 273, 293, 301, 296, 293, 284, 261,
    258, 276, 278, 290, 287, 274, 260, 255,
    241, 259, 270, 277, 276, 262, 260, 237,
    253, 233, 258, 264, 261, 260, 234, 215,

};

int	BBKnightOutpostTable[64] =  // only applied to MG
{
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   8,  12,  12,   8,   0,   0,
    0,   4,  10,  16,  16,  10,   4,   0,
    0,   2,   8,  10,  10,   8,   2,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
};

int	BBPawnMGTable[64] =
{
    100, 100, 100, 100, 100, 100, 100, 100,
    176, 214, 147, 194, 189, 214, 132,  77,
     82,  88, 106, 113, 150, 146, 110,  73,
     67,  93,  83,  95,  97,  92,  99,  63,
     55,  74,  80,  89,  94,  86,  90,  55,
     55,  70,  68,  69,  76,  81, 101,  66,
     52,  84,  66,  60,  69,  99, 117,  60,
    100, 100, 100, 100, 100, 100, 100, 100,
};

int	BBPawnEGTable[64] =
{
    100, 100, 100, 100, 100, 100, 100, 100,
    277, 270, 252, 229, 240, 233, 264, 285,
    190, 197, 182, 168, 155, 150, 180, 181,
    128, 117, 108, 102,  93, 100, 110, 110,
    107, 101,  89,  85,  86,  83,  92,  91,
     96,  96,  85,  92,  88,  83,  85,  82,
    107,  99,  97,  97, 100,  89,  89,  84,
    100, 100, 100, 100, 100, 100, 100, 100,
};

// tuning adjustments to PSTs
#define QUEEN_MG_ADJ   37
#define QUEEN_EG_ADJ   23
#define ROOK_MG_ADJ     1
#define ROOK_EG_ADJ    15 
#define BISHOP_MG_ADJ   2  
#define BISHOP_EG_ADJ   0   
#define KNIGHT_MG_ADJ  17  
#define KNIGHT_EG_ADJ   1   
#define PAWN_MG_ADJ    -1    
#define PAWN_EG_ADJ    -2     

// mobility 
#define N_MOB_THRESHOLD 1 
#define N_MOB_MG        1
#define N_MOB_EG        2
#define B_MOB_THRESHOLD 4 
#define B_MOB_MG        3 
#define B_MOB_EG        3 
#define R_MOB_THRESHOLD 6  
#define R_MOB_MG        3  
#define R_MOB_EG        2  
#define Q_MOB_THRESHOLD 4 
#define Q_MOB_MG        0 
#define Q_MOB_EG        6 

BB_BOARD   *EvalBoard;
int		nEval, nPawnEval;
int		nRow, nCol, nPiece, nColor, nMult, nBlackRow;
SquareType nSquare;
BYTE	nCastleStatus;
int		nPawns[NCOLORS][BSIZE];	// counts pawns in each column, for doubled/tripled pawns and pawn islands

/*========================================================================
** CalcDistance - calculates distance between two squares
**========================================================================
*/
int BBCalcDistance(int sq1, int sq2)
{
    IS_SQ_OK(sq1);
    IS_SQ_OK(sq2);

	return(max(abs((sq2 >> 3) - (sq1 >> 3)), abs((sq1 & 7) - (sq2 & 7))));	// RIGHT!
}

/*========================================================================
** Rook Mobility - Evaluate the mobility of a rook
**========================================================================
*/
int BBRookMobility(int sq, int color)
{
    IS_SQ_OK(sq);
    IS_COLOR_OK(color);

    Bitboard moves = Rmagic(sq, EvalBoard->bbOccupancy);
	INDEX_CHECK(color, EvalBoard->bbMaterial);
    moves &= ~EvalBoard->bbMaterial[color];

    return(BitCount(moves) - R_MOB_THRESHOLD);
}

/*========================================================================
** Bishop Mobility - Evaluate the mobility of a bishop
**========================================================================
*/
int BBBishopMobility(int sq, int color)
{
    IS_SQ_OK(sq);
    IS_COLOR_OK(color);

    Bitboard moves = Bmagic(sq, EvalBoard->bbOccupancy);
	INDEX_CHECK(color, EvalBoard->bbMaterial);
    moves &= ~EvalBoard->bbMaterial[color];

    return(BitCount(moves) - B_MOB_THRESHOLD);
}

/*========================================================================
** Queen Mobility - Evaluate the mobility of a queen
**========================================================================
*/
int	BBQueenMobility(int sq, int color)
{
    IS_SQ_OK(sq);
    IS_COLOR_OK(color);

    Bitboard moves = Bmagic(sq, EvalBoard->bbOccupancy) | Rmagic(sq, EvalBoard->bbOccupancy);
    INDEX_CHECK(color, EvalBoard->bbMaterial);
    moves &= ~EvalBoard->bbMaterial[color];

    return(BitCount(moves) - Q_MOB_THRESHOLD);
}

/*========================================================================
** Knight Mobility - Evaluate the mobility of a knight
**========================================================================
*/
int BBKnightMobility(int sq, int color)
{
    IS_SQ_OK(sq);
    IS_COLOR_OK(color);
	INDEX_CHECK(sq, bbKnightMoves);
    Bitboard moves = bbKnightMoves[sq];
	INDEX_CHECK(color,EvalBoard->bbMaterial);
    moves &= ~EvalBoard->bbMaterial[color];

    return(BitCount(moves) - N_MOB_THRESHOLD);
}

/*========================================================================
** IsPassedPawn
**========================================================================
*/
BOOL IsPassedPawn(BB_BOARD *Board, int sq, int color)
{
    IS_SQ_OK(sq);
    IS_COLOR_OK(color);
	assert(color >= 0 && color < 2);
	assert(sq >= 0 && sq < 64);
	assert(OPPONENT(color)>= 0 && OPPONENT(color) < 2);
    return((bbPassedPawnMask[color][sq] & Board->bbPieces[PAWN][OPPONENT(color)]) == 0);
}

/*========================================================================
** EvaluatePawns - Evaluate the entire pawn structure
**========================================================================
*/
#if USE_PAWN_HASH
void BBEvaluatePawns(PosSignature PawnSig, int *mgEval, int *egEval)
#else
void BBEvaluatePawns(int *mgEval, int *egEval)
#endif
{
    Bitboard pawns;
    int	n, sq;
    int pmgEval, pegEval;
	int	nNumColPawnsOuter[10]={0};
	int *nNumColPawns = &nNumColPawnsOuter[1];

#if USE_PAWN_HASH
    if (!bTuning)
    {
        PAWN_HASH_ENTRY* found = ProbePawnHash(PawnSig);
        if (found)
        {
            *mgEval += found->mgEval;
            *egEval += found->egEval;
            return;
        }
    }
#endif

    pmgEval = pegEval = 0;

    // WHITE PAWNS
    pawns = EvalBoard->bbPieces[PAWN][WHITE];
    if (pawns)
    {
        // Eval by columns -- DOUBLED, TRIPLED, ISOLATED and BLOCKED pawns
        for (n = 0; n < BSIZE; n++)
            nNumColPawns[n] = BitCount(pawns & FileMask[n]);

        for (n = 0; n < BSIZE; n++)
        {
            if (nNumColPawns[n] == 0)
                continue;

            // doubled-tripled white pawns
            if (nNumColPawns[n] >= 2)
            {
                pmgEval -= DOUBLED_PAWN_MG;
                pegEval -= DOUBLED_PAWN_EG;
#if DEBUG_PAWNS
                printf("doubled white pawns in column %d\n", n);
#endif
                // are these pawns also blocked
                Bitboard bpawns = pawns & FileMask[n];	// white pawns on this file
                sq = BitScan(GetLSB(bpawns)); // forward-most pawn
                if (EvalBoard->bbPieces[PAWN][BLACK] & bbPassedPawnMask[WHITE][sq] & FileMask[n])
                {
                    pmgEval -= BLOCKED_DOT_PAWN_MG;
                    pegEval -= BLOCKED_DOT_PAWN_EG;
#if DEBUG_PAWNS
                    printf("   and they're blocked\n");
#endif
                }
            }

            // isolated white pawns
            if (nNumColPawns[n])
            {
                if ((n == 0) && ((nNumColPawns[1]) == 0))
                {
                    pmgEval -= nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval -= nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated white pawn found in column %d\n", n);
#endif
                }
                else if ((n == 7) && ((nNumColPawns[6]) == 0))
                {
                    pmgEval -= nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval -= nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated white pawn found in column %d\n", n);
#endif
                }
                else if (((nNumColPawns[n-1]) == 0) && ((nNumColPawns[n+1]) == 0))
                {
                    pmgEval -= nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval -= nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated white pawn found in column %d\n", n);
#endif
                }
            }
        }

        // PASSERS
        while (pawns)
        {
            DWORD pawnsq = BitScan(PopLSB(&pawns));

            if (IsPassedPawn(EvalBoard, pawnsq, WHITE))
            {
                int file = File(pawnsq);
                int rank = Rank(pawnsq);

#if DEBUG_PAWNS
                printf("white passed pawn found at square %d\n", pawnsq);
#endif

                if (rank != RANK_7) // PSTs already add bonus for reaching the 7th
                {
                    pmgEval += nPasserBonusMG[WHITE][rank];
                    pegEval += nPasserBonusEG[WHITE][rank];
                }

                // protected passed
                if (((file != FILE_A) && (EvalBoard->squares[pawnsq + 7] == WHITE_PAWN)) ||
                        ((file != FILE_H) && (EvalBoard->squares[pawnsq + 9] == WHITE_PAWN)))
                {
                    pmgEval += PROTECTED_PASSER_MG;
                    pegEval += PROTECTED_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and it is protected\n");
#endif
                }

                // connected passed
                if (((file != FILE_A) && (EvalBoard->squares[pawnsq - 1] == WHITE_PAWN) && IsPassedPawn(EvalBoard, pawnsq - 1, WHITE)) ||
                        ((file != FILE_H) && (EvalBoard->squares[pawnsq + 1] == WHITE_PAWN) && IsPassedPawn(EvalBoard, pawnsq + 1, WHITE)))
                {
                    pmgEval += CONNECTED_PASSER_MG;
                    pegEval += CONNECTED_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and it is connected\n");
#endif
                }

#if 0
                // rook behind passer - no ELO improvement!
                Bitboard rook = EvalBoard->bbPieces[ROOK][WHITE] & FileMask[file] | EvalBoard->bbPieces[QUEEN][WHITE] & FileMask[file];
                if (rook && (BitScan(GetMSB(rook)) > pawnsq))
                {
                    pmgEval += ROOK_BEHIND_PASSER_MG;
                    pegEval += ROOK_BEHIND_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and there is a rook behind it\n");
#endif
                }
#endif

                // bonus/penalty for king distances
                int ksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
                int opksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);
                int kdist = BBCalcDistance(pawnsq, ksq);
                int opkdist = BBCalcDistance(pawnsq, opksq);
                if (kdist < opkdist)
                    pegEval += nPasserKingDist[opkdist - kdist];
                else if (opkdist < kdist)
                    pegEval -= nPasserKingDist[kdist - opkdist];
            }
        }
    }

    // BLACK PAWNS
    pawns = EvalBoard->bbPieces[PAWN][BLACK];
    if (pawns)
    {
        // Eval by columns -- DOUBLED, TRIPLED, ISOLATED and BLOCKED pawns
        for (n = 0; n < BSIZE; n++)
            nNumColPawns[n] = BitCount(pawns & FileMask[n]);

        for (n = 0; n < BSIZE; n++)
        {
            if (nNumColPawns[n] == 0)
                continue;

            // doubled-tripled black pawns
            if (nNumColPawns[n] >= 2)
            {
                pmgEval += DOUBLED_PAWN_MG;
                pegEval += DOUBLED_PAWN_EG;
#if DEBUG_PAWNS
                printf("doubled black pawns in column %d\n", n);
#endif
                // are these pawns also blocked
                Bitboard bpawns = pawns & FileMask[n];	// black pawns on this file
                sq = BitScan(GetMSB(bpawns)); // forward-most pawn
                if (EvalBoard->bbPieces[PAWN][WHITE] & bbPassedPawnMask[BLACK][sq] & FileMask[n])
                {
                    pmgEval += BLOCKED_DOT_PAWN_MG;
                    pegEval += BLOCKED_DOT_PAWN_EG;
#if DEBUG_PAWNS
                    printf("   and they're blocked\n");
#endif
                }
            }

            // isolated black pawns
            if (nNumColPawns[n])
            {
                if ((n == 0) && ((nNumColPawns[1]) == 0))
                {
                    pmgEval += nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval += nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated black pawn found in column %d\n", n);
#endif
                }
                else if ((n == 7) && ((nNumColPawns[6]) == 0))
                {
                    pmgEval += nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval += nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated black pawn found in column %d\n", n);
#endif
                }
                else if (((nNumColPawns[n-1]) == 0) && ((nNumColPawns[n+1]) == 0))
                {
                    pmgEval += nNumColPawns[n] * ISOLATED_PAWN_MG;
                    pegEval += nNumColPawns[n] * ISOLATED_PAWN_EG;
#if DEBUG_PAWNS
                    printf("isolated black pawn found in column %d\n", n);
#endif
                }
            }
    }

        // PASSERS
        while (pawns)
        {
            DWORD pawnsq = BitScan(PopLSB(&pawns));

            if (IsPassedPawn(EvalBoard, pawnsq, BLACK))
            {
                int file = File(pawnsq);
                int rank = Rank(pawnsq);

#if DEBUG_PAWNS
                printf("black passed pawn found at square %d\n", pawnsq);
#endif

                if (rank != RANK_2) // PSTs already add bonus for reaching 2nd
                {
                    pmgEval -= nPasserBonusMG[BLACK][rank];
                    pegEval -= nPasserBonusEG[BLACK][rank];
                }

                // protected passed
                if (((file != FILE_H) && (EvalBoard->squares[pawnsq - 7] == BLACK_PAWN)) ||
                        ((file != FILE_A) && (EvalBoard->squares[pawnsq - 9] == BLACK_PAWN)))
                {
                    pmgEval -= PROTECTED_PASSER_MG;
                    pegEval -= PROTECTED_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and it is protected\n");
#endif
                }

                // connected passed
                if (((file != FILE_A) && (EvalBoard->squares[pawnsq - 1] == BLACK_PAWN) && IsPassedPawn(EvalBoard, pawnsq - 1, BLACK)) ||
                        ((file != FILE_H) && (EvalBoard->squares[pawnsq + 1] == BLACK_PAWN) && IsPassedPawn(EvalBoard, pawnsq + 1, BLACK)))
                {
                    pmgEval -= CONNECTED_PASSER_MG;
                    pmgEval -= CONNECTED_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and it is connected\n");
#endif
                }

#if 0
                // rook behind passer - no ELO improvement!
				INDEX_CHECK(file, FileMask);
                Bitboard rook = EvalBoard->bbPieces[ROOK][BLACK] & FileMask[file] | EvalBoard->bbPieces[QUEEN][BLACK] & FileMask[file];
                if (rook && (BitScan(GetLSB(rook)) < pawnsq))
                {
                    pmgEval -= ROOK_BEHIND_PASSER_MG;
                    pegEval -= ROOK_BEHIND_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and there is a rook behind it\n");
#endif
                }
#endif

                // bonus/penalty for king distances
                int ksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);
                int opksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
                int kdist = BBCalcDistance(pawnsq, ksq);
                int opkdist = BBCalcDistance(pawnsq, opksq);
                if (kdist < opkdist)
                    pegEval -= nPasserKingDist[opkdist - kdist];
                else if (opkdist < kdist)
                    pegEval += nPasserKingDist[kdist - opkdist];
            }
        }
    }

#if USE_PAWN_HASH
    SavePawnHash(pmgEval, pegEval, PawnSig);
#endif

    *mgEval += pmgEval;
    *egEval += pegEval;
}

/*========================================================================
** OpenFile - is this file open or semi-open
**========================================================================
*/
int	BBOpenFile(int file, int color)
{

    if ((FileMask[file] & (EvalBoard->bbPieces[PAWN][WHITE] | EvalBoard->bbPieces[PAWN][BLACK])) == 0)
        return(OPEN_FILE);

    if ((FileMask[file] & EvalBoard->bbPieces[PAWN][OPPONENT(color)]) == 0)
        return(SEMIOPEN_FILE);

    return(NOT_OPEN);
}

/*========================================================================
** GetTaperedEval - Return single eval score based on midgame and endgame
** evaluation and total wood (phase)
**========================================================================
*/
int GetTaperedEval(int nPhase, int mgEval, int egEval)
{
    return((egEval * (TOTAL_PHASE - nPhase)) + (mgEval * nPhase)) / TOTAL_PHASE;
}

/*========================================================================
** Evaluate - assign a "goodness" score to the current position on the
** eval board
**========================================================================
*/
int BBEvaluate(BB_BOARD *Board, int nAlpha, int nBeta)
{
    int			sq, ptsq;
    int			piece, row, col, mult, color;
    int			nEval, mgEval, egEval;
#if USE_PAWN_HASH
    PosSignature PawnSig = 0;
#endif

    EvalBoard = Board;

    if (bTuning)
        goto FullEval;

#if USE_EVAL_HASH
    EVAL_HASH_ENTRY *found = ProbeEvalHash(Board->signature);
    if (found)
    {
        if (found->nEval < nAlpha)
            return(nAlpha);
        if (found->nEval > nBeta)
            return(nBeta);
        return(found->nEval);
    }
#endif

    if (!tb_available)  // if tablebases are not available, check for material draw and do special code for KPvK ending
    {
        int nTotalPieces = BitCount(EvalBoard->bbOccupancy);

        if (nTotalPieces == 2)
        {
            nEval = 0;	// just kings on board
            goto exit;
        }

        if ((nTotalPieces == 3) && (EvalBoard->phase == MINOR_PHASE))
        {
            nEval = 0;	// king and minor vs king
            goto exit;
        }

        if (nTotalPieces == 4)
        {
            if ((EvalBoard->phase == (2 * MINOR_PHASE)) && (BitCount(EvalBoard->bbMaterial[WHITE] == 2)))
            {
                nEval = 0;	// both sides have one minor, although, strictly speaking, it is possible to mate in these positions
                goto exit;
            }

            if ((BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] == 2)) || (BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] == 2)))
            {
                nEval = 0;	// KNNvK = DRAW!
                goto exit;
            }

            // rook vs minor
            if (BitCount(EvalBoard->bbPieces[ROOK][WHITE] == 1))
            {
                if ((BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] == 1)) || (BitCount(EvalBoard->bbPieces[BISHOP][BLACK] == 1)))
                {
                    nEval = 0;
                    goto exit;
                }
            }

            if (BitCount(EvalBoard->bbPieces[ROOK][BLACK] == 1))
            {
                if ((BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] == 1)) || (BitCount(EvalBoard->bbPieces[BISHOP][WHITE] == 1)))
                {
                    nEval = 0;
                    goto exit;
                }
            }
        }

        // rook and minor vs rook
        if ((nTotalPieces == 5) && (EvalBoard->phase == ((ROOK_PHASE * 2) + MINOR_PHASE)))
        {
            if ((BitCount(EvalBoard->bbPieces[ROOK][WHITE])) && (BitCount(EvalBoard->bbPieces[ROOK][BLACK])))
            {
                nEval = 0;
                goto exit;
            }
        }
    }

FullEval:

    mgEval = egEval = 0;
    int nMob;

    // eval all wood except kings, which come later
    for (piece = QUEEN; piece <= PAWN; piece++)
    {
        for (color = WHITE; color <= BLACK; color++)
        {
            Bitboard pieces = EvalBoard->bbPieces[piece][color];

            if (pieces == 0)
                continue;

            while (pieces)
            {
                ptsq = sq = BitScan(PopLSB(&pieces));
                row = sq >> 3;
                col = sq & 7;

                if (color == BLACK)
                    ptsq = ((7 - row) << 3) + col;	// piece square table conversion

                // get piece info
                mult = (color == WHITE ? 1 : -1);

                // piece square table lookup
                switch (piece)
                {
                    case QUEEN:
                        mgEval += (BBQueenMGTable[ptsq] + QUEEN_MG_ADJ) * mult;
                        egEval += (BBQueenEGTable[ptsq] + QUEEN_EG_ADJ) * mult;
                        nMob = BBQueenMobility(sq, color);
                        mgEval += nMob * Q_MOB_MG * mult;
                        egEval += nMob * Q_MOB_EG * mult;
                        break;

                    case ROOK:
                        mgEval += (BBRookMGTable[ptsq] + ROOK_MG_ADJ) * mult;
                        egEval += (BBRookEGTable[ptsq] + ROOK_EG_ADJ) * mult;
                        nMob = BBRookMobility(sq, color);
                        mgEval += nMob * R_MOB_MG * mult;
                        egEval += nMob * R_MOB_EG * mult;
                        if (BBOpenFile(col, color) == OPEN_FILE)
                            mgEval += OPEN_FILE_MG * mult;
                        break;

                    case BISHOP:
                        mgEval += (BBBishopMGTable[ptsq] + BISHOP_MG_ADJ) * mult;
                        egEval += (BBBishopEGTable[ptsq] + BISHOP_EG_ADJ) * mult;
                        nMob = BBBishopMobility(sq, color);
                        mgEval += nMob * B_MOB_MG * mult;
                        egEval += nMob * B_MOB_EG * mult;
                        break;

                    case KNIGHT:
                        mgEval += (BBKnightMGTable[ptsq] + KNIGHT_MG_ADJ) * mult;
                        egEval += (BBKnightEGTable[ptsq] + KNIGHT_EG_ADJ) * mult;
                        nMob = BBKnightMobility(sq, color);
                        mgEval += nMob * N_MOB_MG * mult;
                        egEval += nMob * N_MOB_EG * mult;

                        // outposts
                        if ((col > FILE_A) && (col < FILE_H))	// no outposts on a or h file
                        {
                            if (color == WHITE)
                            {
                                if (row <= RANK_4)	// enemy territory
                                {
                                    // protected by pawn?
                                    if ((EvalBoard->squares[sq + 7] == WHITE_PAWN) ||
                                            (EvalBoard->squares[sq + 9] == WHITE_PAWN))
                                    {
                                        // can't be attacked by enemy pawn?
                                        if ((EvalBoard->squares[sq - 7] != BLACK_PAWN) &&
                                                (EvalBoard->squares[sq - 9] != BLACK_PAWN) &&
                                                (EvalBoard->squares[sq - 15] != BLACK_PAWN) &&
                                                (EvalBoard->squares[sq - 17] != BLACK_PAWN))
                                        {
                                            mgEval += BBKnightOutpostTable[ptsq];
                                        }
                                    }
                                }
                            }
                            else	// BLACK
                            {
                                if (row >= RANK_5)	// enemy territory
                                {
                                    // protected by pawn?
                                    if ((EvalBoard->squares[sq - 7] == BLACK_PAWN) ||
                                            (EvalBoard->squares[sq - 9] == BLACK_PAWN))
                                    {
                                        // can't be attacked by enemy pawn?
                                        if ((EvalBoard->squares[sq + 7] != WHITE_PAWN) &&
                                                (EvalBoard->squares[sq + 9] != WHITE_PAWN) &&
                                                (EvalBoard->squares[sq + 15] != WHITE_PAWN) &&
                                                (EvalBoard->squares[sq + 17] != WHITE_PAWN))
                                        {
                                            mgEval -= BBKnightOutpostTable[ptsq];
                                        }
                                    }
                                }
                            }
                        }
                        break;

                    case PAWN:
                    {
                        mgEval += (BBPawnMGTable[ptsq] + PAWN_MG_ADJ) * mult;
                        egEval += (BBPawnEGTable[ptsq] + PAWN_EG_ADJ) * mult;
#if USE_PAWN_HASH
                        PawnSig ^= aPArray[PAWN + (color * 6)][sq];
#endif
                        break;
                    }
                }
            }
        }
    } // piece counting loop

#if USE_PAWN_HASH
    // add kings to pawn hash signature
    PawnSig ^= aPArray[KING + (WHITE * 6)][BitScan(EvalBoard->bbPieces[KING][WHITE])];
    PawnSig ^= aPArray[KING + (BLACK * 6)][BitScan(EvalBoard->bbPieces[KING][BLACK])];
#endif
    if (bEval)
        printf("Eval = %d/%d after piece counting\n", mgEval, egEval);

    // we're done with all of the piece table stuff and have filled the pawn structure, so evaluate it
#if USE_PAWN_HASH
    BBEvaluatePawns(PawnSig, &mgEval, &egEval);
//  assert(PawnSig == GetBBPawnSignature(Board));
#else
    BBEvaluatePawns(&mgEval, &egEval);
#endif
    if (bEval)
        printf("Eval = %d/%d after pawn eval\n", mgEval, egEval);

    // now add the eval for the Kings
    int		wksq, bksq, ptbksq;
    wksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
    mgEval += BBKingMGTable[wksq];
    egEval += BBKingEGTable[wksq];

    ptbksq = bksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);
    ptbksq = ((7 - (bksq >> 3)) << 3) + (bksq & 7);
    mgEval -= BBKingMGTable[ptbksq];
    egEval -= BBKingEGTable[ptbksq];

    // bishop pair
    if (BitCount(EvalBoard->bbPieces[BISHOP][WHITE]) == 2)
    {
        mgEval += BISHOP_PAIR_MG;
        egEval += BISHOP_PAIR_EG;
    }
    if (BitCount(EvalBoard->bbPieces[BISHOP][BLACK]) == 2)
    {
        mgEval -= BISHOP_PAIR_MG;
        egEval -= BISHOP_PAIR_EG;
    }

    if (bEval)
        printf("Eval = %d/%d before King Safety\n", mgEval, egEval);

    // king safety
    col = File(wksq);
    row = Rank(wksq);

    // eval White King
    // check for pawns shielding the king, penalty applies to middlegame eval only
    if (col >= FILE_F)
    {
        if (EvalBoard->squares[BB_F2] == WHITE_PAWN)
            ;
        else if (EvalBoard->squares[BB_F3] == WHITE_PAWN)
            mgEval -= F3_PAWN_SHIELD;
        else
            mgEval -= NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_G2] == WHITE_PAWN || EvalBoard->squares[BB_G3] == WHITE_PAWN)
            ;
        else
            mgEval -= NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_H2] == WHITE_PAWN || EvalBoard->squares[BB_H3] == WHITE_PAWN)
            ;
        else
            mgEval -= NO_PAWN_SHIELD;
    }
    else if (col <= FILE_C)
    {
        if (EvalBoard->squares[BB_C2] == WHITE_PAWN)
            ;
        else if (EvalBoard->squares[BB_C3] == WHITE_PAWN)
            mgEval -= C3_PAWN_SHIELD;
        else
            mgEval -= NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_B2] == WHITE_PAWN || EvalBoard->squares[BB_B3] == WHITE_PAWN)
            ;
        else
            mgEval -= NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_A2] == WHITE_PAWN || EvalBoard->squares[BB_A3] == WHITE_PAWN)
            ;
        else
            mgEval -= NO_PAWN_SHIELD;
    }

    // now check for opponent pieces attacking the vicinity of the king, penalty applies to middlegame eval only
    Bitboard squares = bbKingMoves[wksq] | EvalBoard->bbPieces[KING][WHITE];

    while (squares)
    {
        int	attacks = 0;
        int square = BitScan(PopLSB(&squares));

        // add diagonal attacks with bishop and queen
        attacks += BitCount((EvalBoard->bbPieces[BISHOP][BLACK] | EvalBoard->bbPieces[QUEEN][BLACK]) & bbDiagonalMoves[square]);

        // add straight attacks with rook and queen
        attacks += BitCount((EvalBoard->bbPieces[ROOK][BLACK] | EvalBoard->bbPieces[QUEEN][BLACK]) & bbStraightMoves[square]);

        // add knight attacks
        attacks += BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] & bbKnightMoves[square]);

        mgEval -= attacks * KING_ATTACKER;
        // double the penalty for direct attacks on the king
        if (square == wksq)
            mgEval -= attacks * KING_ATTACKER;
    }

    // eval Black King
    // check for pawns shielding the king, penalty applies to middlegame eval only
    col = File(bksq);
    row = Rank(bksq);

    if (col >= FILE_F)
    {
        if (EvalBoard->squares[BB_F7] == BLACK_PAWN)
            ;
        else if (EvalBoard->squares[BB_F6] == BLACK_PAWN)
            mgEval += F6_PAWN_SHIELD;
        else
            mgEval += NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_G7] == BLACK_PAWN || EvalBoard->squares[BB_G6] == BLACK_PAWN)
            ;
        else
            mgEval += NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_H7] == BLACK_PAWN || EvalBoard->squares[BB_H6] == BLACK_PAWN)
            ;
        else
            mgEval += NO_PAWN_SHIELD;
    }
    else if (col <= FILE_C)
    {
        if (EvalBoard->squares[BB_C7] == BLACK_PAWN)
            ;
        else if (EvalBoard->squares[BB_C6] == BLACK_PAWN)
            mgEval += C6_PAWN_SHIELD;
        else
            mgEval += NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_B7] == BLACK_PAWN || EvalBoard->squares[BB_B6] == BLACK_PAWN)
            ;
        else
            mgEval += NO_PAWN_SHIELD;

        if (EvalBoard->squares[BB_A7] == BLACK_PAWN || EvalBoard->squares[BB_A6] == BLACK_PAWN)
            ;
        else
            mgEval += NO_PAWN_SHIELD;
    }

    // now check for opponent pieces attacking the vicinity of the king
    squares = bbKingMoves[bksq] | EvalBoard->bbPieces[KING][BLACK];

    while (squares)
    {
        int	attacks = 0;
        int square = BitScan(PopLSB(&squares));

        // add diagonal attacks with bishop and queen
        attacks += BitCount((EvalBoard->bbPieces[BISHOP][WHITE] | EvalBoard->bbPieces[QUEEN][WHITE]) & bbDiagonalMoves[square]);

        // add straight attacks with rook and queen
        attacks += BitCount((EvalBoard->bbPieces[ROOK][WHITE] | EvalBoard->bbPieces[QUEEN][WHITE]) & bbStraightMoves[square]);

        // add knight attacks
        attacks += BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] & bbKnightMoves[square]);

        // double the penalty for direct attacks on the king
        mgEval += attacks * KING_ATTACKER;
        if (square == bksq)
            mgEval += attacks * KING_ATTACKER;
    }

    if (bEval)
        printf("Eval = %d/%d after king safety\n", mgEval, egEval);

    // perform tapered eval calculation according to current board phase, then add to initial eval (which was just material imbalance)
    nEval = GetTaperedEval(EvalBoard->phase, mgEval, egEval);

    if (bEval)
        printf("Final Eval = %d (%d phase)\n", nEval, EvalBoard->phase);

    // Winning side has only two knights -- no better than draw
    if ((nEval > 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][WHITE]) == 2) && 
        ((EvalBoard->bbMaterial[WHITE]) == (EvalBoard->bbPieces[KNIGHT][WHITE] | EvalBoard->bbPieces[KING][WHITE])))
    {
        nEval = 0;
        goto exit;
    }
    else if ((nEval < 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][BLACK]) == 2) &&
        ((EvalBoard->bbMaterial[BLACK]) == (EvalBoard->bbPieces[KNIGHT][BLACK] | EvalBoard->bbPieces[KING][BLACK])))
    {
        nEval = 0;
        goto exit;
    }

#if KING_AND_MINOR_DRAW	// this may be true in normal play, but not in some studies. Hence the compiler switch
    // Winning side has only one minor -- no better than draw
    if ((nEval > 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] | BitCount(EvalBoard->bbPieces[BISHOP][WHITE])) == 1) &&
        ((EvalBoard->bbMaterial[WHITE]) == (EvalBoard->bbPieces[KNIGHT][WHITE] | EvalBoard->bbPieces[BISHOP][WHITE] | EvalBoard->bbPieces[KING][WHITE])))
    {
        nEval = 0;
        goto exit;
    }
    if ((nEval < 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] | BitCount(EvalBoard->bbPieces[BISHOP][BLACK])) == 1) &&
        ((EvalBoard->bbMaterial[BLACK]) == (EvalBoard->bbPieces[KNIGHT][BLACK] | EvalBoard->bbPieces[BISHOP][BLACK] | EvalBoard->bbPieces[KING][BLACK])))
    {
        nEval = 0;
        goto exit;
    }
#endif

    if (EvalBoard->sidetomove == BLACK)
        nEval = -nEval;

exit:
#if USE_EVAL_HASH
    SaveEvalHash(nEval, EvalBoard->signature);
#endif

    if (nEval < nAlpha)
        return(nAlpha);
    else if (nEval > nBeta)
        return(nBeta);
    else
        return(nEval);
}

/*========================================================================
** FastEvaluate - evaluation based on material and PST only, also checks for material draw
**========================================================================
*/
int FastEvaluate(BB_BOARD* Board)
{
    int	sq, ptsq;
    int	piece, row, col, mult, color;
    int	mgEval, egEval;

#if USE_EVAL_HASH
    EVAL_HASH_ENTRY* found = ProbeEvalHash(Board->signature);
    if (found)
        return(found->nEval);
#endif

    EvalBoard = Board;

    if (!tb_available)  // if tablebases are not available, check for material draw and do special code for KPvK ending
    {
        int nTotalPieces = BitCount(EvalBoard->bbOccupancy);

        if (nTotalPieces == 2)
            return(0);	// just kings on board

        if ((nTotalPieces == 3) && (EvalBoard->phase == MINOR_PHASE))
            return(0);	// just kings on board

        if (nTotalPieces == 4)
        {
            if ((EvalBoard->phase == (2 * MINOR_PHASE)) && (BitCount(EvalBoard->bbMaterial[WHITE] == 2)))
                return(0);	// both sides have one minor, although, strictly speaking, it is possible to mate in these positions

            if ((BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] == 2)) || (BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] == 2)))
                return(0);	// KNNvK = DRAW!

            // rook vs minor
            if (BitCount(EvalBoard->bbPieces[ROOK][WHITE] == 1))
            {
                if ((BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] == 1)) || (BitCount(EvalBoard->bbPieces[BISHOP][BLACK] == 1)))
                    return(0);
            }

            if (BitCount(EvalBoard->bbPieces[ROOK][BLACK] == 1))
            {
                if ((BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] == 1)) || (BitCount(EvalBoard->bbPieces[BISHOP][WHITE] == 1)))
                    return(0);
            }
        }

        // rook and minor vs rook
        if ((nTotalPieces == 5) && (EvalBoard->phase == ((ROOK_PHASE * 2) + MINOR_PHASE)))
        {
            if ((BitCount(EvalBoard->bbPieces[ROOK][WHITE])) && (BitCount(EvalBoard->bbPieces[ROOK][BLACK])))
                return(0);
        }
    }

    mgEval = egEval = 0;

    // eval all wood except kings, which come later
    for (piece = QUEEN; piece <= PAWN; piece++)
    {
        for (color = WHITE; color <= BLACK; color++)
        {
            Bitboard pieces = EvalBoard->bbPieces[piece][color];

            if (pieces == 0)
                continue;

            while (pieces)
            {
                ptsq = sq = BitScan(PopLSB(&pieces));
                row = sq >> 3;
                col = sq & 7;

                if (color == BLACK)
                    ptsq = ((7 - row) << 3) + col;	// piece square table conversion

                // get piece info
                mult = (color == WHITE ? 1 : -1);

                // piece square table lookup
                switch (piece)
                {
                    case QUEEN:
                        mgEval += BBQueenMGTable[ptsq] * mult;
                        egEval += BBQueenEGTable[ptsq] * mult;
                        break;

                    case ROOK:
                        mgEval += BBRookMGTable[ptsq] * mult;
                        egEval += BBRookEGTable[ptsq] * mult;
                        break;

                    case BISHOP:
                        mgEval += BBBishopMGTable[ptsq] * mult;
                        egEval += BBBishopEGTable[ptsq] * mult;
                        break;

                    case KNIGHT:
                        mgEval += BBKnightMGTable[ptsq] * mult;
                        egEval += BBKnightEGTable[ptsq] * mult;
                        break;

                    case PAWN:
                    {
                        mgEval += BBPawnMGTable[ptsq] * mult;
                        egEval += BBPawnEGTable[ptsq] * mult;
                        break;
                    }
                }
            }
        }
    } // piece counting loop

    return(GetTaperedEval(EvalBoard->phase, mgEval, egEval));
}

/*========================================================================
** EPDTune - run through EPD file, collect static eval scores and produce
** overall "accuracy" score via sigmoid function
**========================================================================
*/
double EPDTune(char* filename, double K)
{
    char    pos[128];
    int     dummy, n, eval;
    double  E, R, sigmoid;
    FILE*   epd = fopen(filename, "r");

    if (!epd)
    {
        printf("Unable to open EPD file, %d\n", errno);
        return(0.0);
    }

    n = 0;
    E = 0.0;

    while (fgets(pos, 128, epd))
    {
        BBForsytheToBoard(pos, &bbBoard, &dummy);
        eval = BBEvaluate(&bbBoard, -INFINITY, INFINITY);
        if (bbBoard.sidetomove == BLACK)
            eval *= -1;

        // get game result
        if (strstr(pos, "1-0"))
            R = 1.0;
        else if (strstr(pos, "1/2"))
            R = 0.5;
        else
            R = 0.0;

        // compute sigmoid
        sigmoid = (1 / (1 + pow(10, (-K * eval / 400.0))));
        E += pow(R - sigmoid, 2);

        n++;
    }

    printf("E = %f, MSE = %f\n", E, E / (float)n);
    fclose(epd);
    return(E);
}
