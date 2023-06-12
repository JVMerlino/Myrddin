/* 
Myrddin XBoard / WinBoard compatible chess engine written in C
Copyright(C) 2023  John Merlino

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
#include "magicmoves.h"
#include "Hash.h"
#include "Eval.h"
#include "TBProbe.h"
#include "MoveGen.h"
#include "FEN.h"
#include "PArray.inc"

int mg, eg, og; // for tuning

// pawn vals
#define DEBUG_PAWNS			FALSE

#define DOUBLED_PAWN_MG		8 	// penalty for doubled/tripled pawns
#define DOUBLED_PAWN_EG		25
#define BLOCKED_DOT_PAWN_MG 2   // penalty for doubled/tripled pawns that are also blocked
#define BLOCKED_DOT_PAWN_EG -1
#define ISOLATED_PAWN_MG	13  // penalty for isolated pawn
#define ISOLATED_PAWN_EG	6
#define PROTECTED_PASSER_MG 7   // bonus for protected passer
#define PROTECTED_PASSER_EG 15
#define CONNECTED_PASSER_MG 4   // bonus for connected passer
#define CONNECTED_PASSER_EG 12
#define ROOK_BEHIND_PASSER_MG  0  // bonus for rook behind passer
#define ROOK_BEHIND_PASSER_EG  7  
#define PASSER_ON_FOURTH_MG -14  // bonuses for passers on specific ranks
#define PASSER_ON_FOURTH_EG 25     
#define PASSER_ON_FIFTH_MG  17     
#define PASSER_ON_FIFTH_EG  41
#define PASSER_ON_SIXTH_MG  19
#define PASSER_ON_SIXTH_EG  48
#define PASSER_KING_DIST_BASE 6
#define PASSER_KING_DIST_MULT 4

// bishop vals
#define BISHOP_PAIR_MG		24  // bonus
#define BISHOP_PAIR_EG		53	// bonus

// rook vals
enum { NOT_OPEN, OPEN_FILE, SEMIOPEN_FILE };
#define OPEN_FILE_MG		29  // rook on open file bonus
#define OPEN_FILE_EG		0

// king safety
#define F3_PAWN_SHIELD		4   // penalty
#define F6_PAWN_SHIELD		F3_PAWN_SHIELD
#define C3_PAWN_SHIELD		10   // penalty
#define C6_PAWN_SHIELD		C3_PAWN_SHIELD
#define NO_PAWN_SHIELD		20  // penalty for no pawn in front of king
#define OPPOSITE_PAWN		-1 	// penalty for no pawn in front of king on semiopen file
#define Q_ATTACKER		    5   // value of each attack on king square or square bordering king - only applied to MG
#define R_ATTACKER		    2	// value of each attack on king square or square bordering king - only applied to MG
#define B_ATTACKER		    4	// value of each attack on king square or square bordering king - only applied to MG
#define N_ATTACKER		    0	// value of each attack on king square or square bordering king - only applied to MG
#define KING_SQ_ATTACKER    3   // additional value of attacking king square directly
#define KING_IN_CENTER      13	// penalty for being on d or e file

#define KNIGHT_OUTPOST_PST 12
#define BISHOP_OUTPOST_PST 13

int PST[14][64] = {
{	// king mg
	-26,  42,  10,   7, -20, -14,  28,  41,
	  0,  52,  20,  33,  23,  38,  27, -14,
	 10,  57,  29,  16,  39,  79,  43,  14,
	-13, -13,  -2, -80, -74, -23, -19, -64,
	-13, -12, -49, -102, -117, -56, -51, -79,
	 -1,   5, -50, -78, -76, -57, -21, -38,
	 57,  19, -10, -48, -48, -25,  15,  29,
	 29,  59,  43, -63,  -1, -33,  31,  37,
},
{	// queen mg
	896, 915, 918, 953, 965, 987, 975, 942,
	923, 904, 932, 913, 921, 958, 944, 999,
	940, 933, 946, 950, 958, 1004, 1006, 995,
	923, 932, 945, 936, 937, 947, 950, 955,
	935, 932, 927, 934, 944, 941, 950, 952,
	935, 939, 936, 944, 951, 948, 957, 953,
	934, 941, 944, 958, 955, 963, 961, 967,
	929, 931, 946, 952, 950, 933, 934, 942,
},
{	// rook mg
	478, 499, 473, 497, 501, 499, 529, 507,
	460, 455, 508, 500, 497, 530, 502, 531,
	448, 475, 477, 476, 497, 513, 547, 507,
	447, 460, 463, 467, 465, 486, 497, 478,
	441, 450, 442, 455, 456, 457, 484, 460,
	442, 449, 449, 452, 466, 470, 493, 475,
	444, 458, 449, 460, 466, 476, 492, 451,
	459, 461, 475, 475, 481, 473, 477, 458,
},
{	// bishop mg
	300, 306, 275, 269, 266, 278, 332, 279,
	321, 337, 348, 320, 351, 354, 337, 339,
	335, 361, 354, 377, 368, 391, 380, 365,
	328, 344, 369, 369, 366, 365, 348, 333,
	335, 342, 343, 363, 361, 348, 337, 344,
	341, 352, 343, 351, 356, 354, 353, 360,
	351, 351, 362, 336, 346, 368, 366, 358,
	316, 353, 335, 331, 336, 328, 355, 341,
},
{	// knight mg
	150, 227, 266, 304, 354, 257, 292, 227,
	298, 315, 370, 373, 368, 418, 334, 357,
	316, 364, 376, 386, 425, 430, 394, 360,
	325, 336, 357, 382, 369, 393, 359, 368,
	313, 326, 334, 341, 351, 347, 361, 325,
	293, 317, 331, 342, 356, 339, 340, 316,
	285, 294, 304, 325, 327, 331, 314, 323,
	235, 294, 289, 306, 305, 315, 295, 278,
},
{	// pawn mg
	100, 100, 100, 100, 100, 100, 100, 100,
	130, 161, 146, 181, 166, 135,  83,  51,
	 76,  87, 124, 129, 127, 161, 138,  96,
	 61,  78,  87,  97, 117, 108, 107,  91,
	 56,  72,  70,  94,  95,  94,  98,  73,
	 53,  68,  67,  68,  82,  75,  90,  70,
	 54,  71,  57,  60,  73,  86, 105,  62,
	100, 100, 100, 100, 100, 100, 100, 100,
},
{	// king eg
	-78, -49, -39, -18, -17,  -3,   1, -82,
	-19,   7,   4,   7,  17,  33,  35,   9,
	 -7,  13,  23,  28,  33,  35,  39,   8,
	-12,  20,  33,  44,  46,  44,  34,  14,
	-23,   6,  28,  44,  45,  34,  20,   7,
	-25,  -2,  19,  28,  29,  23,   5,  -2,
	-29, -11,  10,  10,  14,   9,  -7, -21,
	-53, -44, -19, -10, -30,  -9, -35, -58,
},
{	// queen eg
	952, 957, 962, 967, 958, 947, 948, 950,
	942, 968, 973, 996, 1010, 976, 973, 937,
	941, 949, 964, 976, 987, 956, 940, 940,
	954, 961, 958, 971, 992, 983, 980, 973,
	943, 961, 945, 977, 959, 966, 953, 959,
	932, 941, 946, 939, 946, 948, 937, 938,
	929, 921, 912, 920, 924, 898, 884, 881,
	920, 917, 939, 924, 923, 917, 919, 902,
},
{	// rook eg
	528, 525, 528, 527, 525, 527, 523, 523,
	532, 545, 534, 533, 530, 519, 526, 512,
	535, 533, 534, 528, 521, 515, 511, 510,
	534, 530, 539, 531, 523, 516, 514, 515,
	528, 528, 526, 525, 523, 522, 509, 513,
	521, 520, 520, 523, 514, 506, 490, 494,
	514, 514, 525, 520, 512, 505, 495, 508,
	515, 514, 535, 517, 508, 513, 504, 504,
},
{	// bishop eg
	300, 298, 292, 304, 305, 297, 296, 289,
	282, 291, 295, 300, 284, 287, 297, 277,
	304, 293, 295, 283, 289, 293, 294, 292,
	296, 301, 296, 300, 294, 298, 293, 297,
	294, 301, 299, 299, 295, 295, 296, 284,
	287, 298, 295, 298, 303, 292, 291, 283,
	291, 275, 285, 297, 294, 284, 282, 277,
	280, 297, 285, 291, 289, 293, 278, 270,
},
{	// knight eg
	238, 267, 268, 272, 268, 263, 269, 199,
	265, 278, 270, 281, 269, 260, 273, 243,
	275, 281, 295, 292, 279, 270, 273, 261,
	279, 294, 306, 303, 302, 297, 289, 270,
	283, 290, 299, 305, 307, 297, 285, 271,
	265, 281, 282, 300, 296, 276, 270, 268,
	264, 273, 279, 279, 279, 273, 265, 269,
	261, 247, 286, 274, 275, 268, 250, 263,
},
{	// pawn eg
	100, 100, 100, 100, 100, 100, 100, 100,
	277, 262, 261, 223, 219, 234, 271, 283,
	189, 190, 156, 135, 129, 129, 167, 160,
	125, 115, 100,  87,  84,  87, 103, 102,
	107, 104,  89,  84,  81,  85,  90,  89,
	100, 102,  88,  90,  89,  90,  85,  85,
	107, 105,  99,  99, 102,  93,  86,  86,
	100, 100, 100, 100, 100, 100, 100, 100,
},
{	// knight outpost
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,  -1,   7,   7,  -6, -13,   0,
	  0,  -3,  21,  27,  20,  32,   8,   0,
	  0,  25,  24,  34,  43,  32,  56,   0,
	  0,  13,  11,  26,  23,  11,  17,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
},
{	// bishop outpost
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   1,   5,  -1,  54,  19,  21,   0,
	  0,   2,   8,  23,  17,  23,   1,   0,
	  0,  29,  10,  27,  40,  18,  40,   0,
	  0,  12,  19,  26,  28,   3,  10,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
},
};

#define K_MOB_THRESHOLD 0
#define K_MOB_MG        -2
#define K_MOB_EG        -1
#define Q_MOB_THRESHOLD 1
#define Q_MOB_MG        1
#define Q_MOB_EG        6
#define R_MOB_THRESHOLD 5  
#define R_MOB_MG        3  
#define R_MOB_EG        3  
#define B_MOB_THRESHOLD 4 
#define B_MOB_MG        4 
#define B_MOB_EG        5 
#define N_MOB_THRESHOLD 0 
#define N_MOB_MG        1
#define N_MOB_EG        3

BB_BOARD   *EvalBoard;
int		nEval;

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
** King Mobility - Evaluate the mobility of a king
**========================================================================
*/
int BBKingMobility(int sq, int color)
{
	IS_SQ_OK(sq);
	IS_COLOR_OK(color);
	INDEX_CHECK(sq, bbKingMoves);
	Bitboard moves = bbKingMoves[sq];
	INDEX_CHECK(color, EvalBoard->bbMaterial);
	moves &= ~EvalBoard->bbMaterial[color];

	return(BitCount(moves) - K_MOB_THRESHOLD);
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
    return((bbPassedPawnMask[color][sq] & Board->bbPieces[PAWN][OPPONENT(color)]) == BB_EMPTY);
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
    int nNumColPawns[BSIZE];

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
        // Eval by columns -- DOUBLED, ISOLATED and BLOCKED pawns
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

                switch (rank)
                {
                    case RANK_6:
                        pmgEval += PASSER_ON_SIXTH_MG;
                        pegEval += PASSER_ON_SIXTH_EG;
                        break;

                    case RANK_5:
                        pmgEval += PASSER_ON_FIFTH_MG;
                        pegEval += PASSER_ON_FIFTH_EG;
                        break;

                    case RANK_4:
                        pmgEval += PASSER_ON_FOURTH_MG;
                        pegEval += PASSER_ON_FOURTH_EG;
                        break;
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

                // rook behind passer
                Bitboard rook = EvalBoard->bbPieces[ROOK][WHITE] & FileMask[file] /* | EvalBoard->bbPieces[QUEEN][WHITE] & FileMask[file] */;
                if (rook && (BitScan(GetMSB(rook)) > pawnsq))
                {
                    pmgEval += ROOK_BEHIND_PASSER_MG;
                    pegEval += ROOK_BEHIND_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and there is a rook behind it\n");
#endif
                }

                // bonus/penalty for king distances
                int ksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
                int opksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);
                int kdist = BBCalcDistance(pawnsq, ksq);
                int opkdist = BBCalcDistance(pawnsq, opksq);
                if (kdist < opkdist)
                    pegEval += PASSER_KING_DIST_BASE + (PASSER_KING_DIST_MULT * (opkdist - kdist));
                else if (opkdist < kdist)
					pegEval -= PASSER_KING_DIST_BASE + (PASSER_KING_DIST_MULT * (kdist - opkdist));
			}
        }
    }

    // BLACK PAWNS
    pawns = EvalBoard->bbPieces[PAWN][BLACK];
    if (pawns)
    {
        // Eval by columns -- DOUBLED, ISOLATED and BLOCKED pawns
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

                switch (rank)
                {
                    case RANK_3:
                        pmgEval -= PASSER_ON_SIXTH_MG;
                        pegEval -= PASSER_ON_SIXTH_EG;
                        break;

                    case RANK_4:
                        pmgEval -= PASSER_ON_FIFTH_MG;
                        pegEval -= PASSER_ON_FIFTH_EG;
                        break;

                    case RANK_5:
                        pmgEval -= PASSER_ON_FOURTH_MG;
                        pegEval -= PASSER_ON_FOURTH_EG;
                        break;
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

                // rook behind passer
				INDEX_CHECK(file, FileMask);
                Bitboard rook = EvalBoard->bbPieces[ROOK][BLACK] & FileMask[file] /* | EvalBoard->bbPieces[QUEEN][BLACK] & FileMask[file] */;
                if (rook && (BitScan(GetLSB(rook)) < pawnsq))
                {
                    pmgEval -= ROOK_BEHIND_PASSER_MG;
                    pegEval -= ROOK_BEHIND_PASSER_EG;
#if DEBUG_PAWNS
                    printf("   and there is a rook behind it\n");
#endif
                }

                // bonus/penalty for king distances
                int ksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);
                int opksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
                int kdist = BBCalcDistance(pawnsq, ksq);
                int opkdist = BBCalcDistance(pawnsq, opksq);
				if (kdist < opkdist)
					pegEval -= PASSER_KING_DIST_BASE + (PASSER_KING_DIST_MULT * (opkdist - kdist));
				else if (opkdist < kdist)
					pegEval += PASSER_KING_DIST_BASE + (PASSER_KING_DIST_MULT * (kdist - opkdist));
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
#if 0
    if ((FileMask[file] & EvalBoard->bbPieces[PAWN][OPPONENT(color)]) == 0)
        return(SEMIOPEN_FILE);
#endif
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

#if USE_FAST_EVAL
    return(FastEvaluate(Board));
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

    if (!tb_available)  // if tablebases are not available, check for material draw
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
	int nMob; 

#if USE_MATERIAL_IMBALANCE
	int wWood, bWood;
	wWood = bWood = 0;
#endif

#if USE_INCREMENTAL_PST
	mgEval = EvalBoard->mgPST;
	egEval = EvalBoard->egPST;
#else
	mgEval = egEval = 0;
#endif

	if (bEval)
		printf("Eval = %d/%d after PST\n", mgEval, egEval);

	// eval all wood except kings, which come later
    for (piece = QUEEN; piece <= PAWN; piece++)
    {
        for (color = WHITE; color <= BLACK; color++)
        {
            Bitboard pieces = EvalBoard->bbPieces[piece][color];

            if (pieces == 0)
                continue;
#if USE_MATERIAL_IMBALANCE
			if (color == WHITE)
				wWood += BitCount(pieces) * nPieceVals[piece];
			else
				bWood += BitCount(pieces) * nPieceVals[piece];
#endif
            while (pieces)
            {
                ptsq = sq = BitScan(PopLSB(&pieces));
                row = sq >> 3;
                col = sq & 7;

                if (color == BLACK)
                    ptsq = VFlipSquare[ptsq];	// flip square vertically to get PST value for black pieces

                // get piece info
                mult = (color == WHITE ? 1 : -1);

                // piece square table lookup
                switch (piece)
                {
                    case QUEEN:
#if !USE_INCREMENTAL_PST
                        mgEval += PST[QUEEN][ptsq] * mult;
						egEval += PST[QUEEN + 6][ptsq] * mult;
#endif
						nMob = BBQueenMobility(sq, color);
                        mgEval += nMob * Q_MOB_MG * mult;
                        egEval += nMob * Q_MOB_EG * mult;
						break;

                    case ROOK:
#if !USE_INCREMENTAL_PST
						mgEval += PST[ROOK][ptsq] * mult;
						egEval += PST[ROOK + 6][ptsq] * mult;
#endif
						nMob = BBRookMobility(sq, color);
                        mgEval += nMob * R_MOB_MG * mult;
                        egEval += nMob * R_MOB_EG * mult;
						if (BBOpenFile(col, color) == OPEN_FILE)
						{
							mgEval += OPEN_FILE_MG * mult;
							egEval += OPEN_FILE_EG * mult;
						}
						break;

                    case BISHOP:
#if !USE_INCREMENTAL_PST
						mgEval += PST[BISHOP][ptsq] * mult;
						egEval += PST[BISHOP + 6][ptsq] * mult;
#endif
						nMob = BBBishopMobility(sq, color);
                        mgEval += nMob * B_MOB_MG * mult;
                        egEval += nMob * B_MOB_EG * mult;

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
                                            mgEval += PST[BISHOP_OUTPOST_PST][ptsq];
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
                                            mgEval -= PST[BISHOP_OUTPOST_PST][ptsq];
                                        }
                                    }
                                }
                            }
                        }
                        break;

                    case KNIGHT:
#if !USE_INCREMENTAL_PST
						mgEval += PST[KNIGHT][ptsq] * mult;
						egEval += PST[KNIGHT + 6][ptsq] * mult;
#endif
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
                                            mgEval += PST[KNIGHT_OUTPOST_PST][ptsq];
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
                                            mgEval -= PST[KNIGHT_OUTPOST_PST][ptsq];
                                        }
                                    }
                                }
                            }
                        }
                        break;

                    case PAWN:
                    {
#if !USE_INCREMENTAL_PST
						mgEval += PST[PAWN][ptsq] * mult;
						egEval += PST[PAWN + 6][ptsq] * mult;
#endif
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
    PawnSig ^= aPArray[KING][BitScan(EvalBoard->bbPieces[KING][WHITE])];
    PawnSig ^= aPArray[KING + 6][BitScan(EvalBoard->bbPieces[KING][BLACK])];
#endif
    if (bEval)
        printf("Eval = %d/%d after piece counting\n", mgEval, egEval);

    // we're done with all of the piece table stuff and have filled the pawn structure, so evaluate it
#if USE_PAWN_HASH
	assert(PawnSig == GetBBPawnSignature(Board));
	BBEvaluatePawns(PawnSig, &mgEval, &egEval);
#else
    BBEvaluatePawns(&mgEval, &egEval);
#endif
    if (bEval)
        printf("Eval = %d/%d after pawn eval\n", mgEval, egEval);

    // now add the eval for the Kings
    int		wksq, bksq;
    wksq = BitScan(EvalBoard->bbPieces[KING][WHITE]);
	bksq = BitScan(EvalBoard->bbPieces[KING][BLACK]);

#if !USE_INCREMENTAL_PST
	mgEval += PST[KING][wksq];
    egEval += PST[KING + 6][wksq];

    int ptbksq = VFlipSquare[bksq];
    mgEval -= PST[KING][ptbksq];
    egEval -= PST[KING + 6][ptbksq];
#endif

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
	// eval White King
	col = File(wksq);
    row = Rank(wksq);

	// king mobility
	nMob = BBKingMobility(wksq, WHITE);
	mgEval += nMob * K_MOB_MG;
	egEval += nMob * K_MOB_EG;

	// check for pawns shielding the king, penalty applies to middlegame eval only
	if (col >= FILE_F)
	{
		if (EvalBoard->squares[BB_F2] == WHITE_PAWN)
			;
		else if (EvalBoard->squares[BB_F3] == WHITE_PAWN)
			mgEval -= F3_PAWN_SHIELD;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_F] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}

		if (EvalBoard->squares[BB_G2] == WHITE_PAWN || EvalBoard->squares[BB_G3] == WHITE_PAWN)
			;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_G] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}

		if (EvalBoard->squares[BB_H2] == WHITE_PAWN || EvalBoard->squares[BB_H3] == WHITE_PAWN)
			;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_H] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}
	}
	else if (col <= FILE_C)
	{
		if (EvalBoard->squares[BB_C2] == WHITE_PAWN)
			;
		else if (EvalBoard->squares[BB_C3] == WHITE_PAWN)
			mgEval -= C3_PAWN_SHIELD;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_C] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}

		if (EvalBoard->squares[BB_B2] == WHITE_PAWN || EvalBoard->squares[BB_B3] == WHITE_PAWN)
			;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_B] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}

		if (EvalBoard->squares[BB_A2] == WHITE_PAWN || EvalBoard->squares[BB_A3] == WHITE_PAWN)
			;
		else
		{
			mgEval -= NO_PAWN_SHIELD;
			if (FileMask[FILE_A] & EvalBoard->bbPieces[PAWN][BLACK])
				mgEval -= OPPOSITE_PAWN;
		}

	}
	else
		mgEval -= KING_IN_CENTER;

    // now check for opponent pieces attacking the vicinity of the king, penalty applies to middlegame eval only
    Bitboard squares = bbKingMoves[wksq] | EvalBoard->bbPieces[KING][WHITE];

    while (squares)
    {
        int attacks, king_sq_attacks;
        BOOL bKingSq = FALSE;
        int square = BitScan(PopLSB(&squares));
        king_sq_attacks = 0;

        if (square == wksq)
            bKingSq = TRUE;

        // queen attacks
		if (EvalBoard->bbPieces[QUEEN][BLACK])
		{
			attacks = BitCount(EvalBoard->bbPieces[QUEEN][BLACK] & bbDiagonalMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval -= attacks * Q_ATTACKER;
			attacks = BitCount(EvalBoard->bbPieces[QUEEN][BLACK] & bbStraightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval -= attacks * Q_ATTACKER;
		}

        // rook attacks
		if (EvalBoard->bbPieces[ROOK][BLACK])
		{
			attacks = BitCount(EvalBoard->bbPieces[ROOK][BLACK] & bbStraightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval -= attacks * R_ATTACKER;
		}

        // bishop attacks
		if (EvalBoard->bbPieces[BISHOP][BLACK])
		{
			attacks = BitCount(EvalBoard->bbPieces[BISHOP][BLACK] & bbDiagonalMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval -= attacks * B_ATTACKER;
		}

        // knight attacks
		if (EvalBoard->bbPieces[KNIGHT][BLACK])
		{
			attacks = BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] & bbKnightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval -= attacks * N_ATTACKER;
		}

        if (bKingSq)
            mgEval -= king_sq_attacks * KING_SQ_ATTACKER;
    }

    // eval Black King
    col = File(bksq);
    row = Rank(bksq);

	// king mobility
	nMob = BBKingMobility(bksq, BLACK);
	mgEval -= nMob * K_MOB_MG;
	egEval -= nMob * K_MOB_EG;

	// check for pawns shielding the king, penalty applies to middlegame eval only
    if (col >= FILE_F)
    {
        if (EvalBoard->squares[BB_F7] == BLACK_PAWN)
            ;
        else if (EvalBoard->squares[BB_F6] == BLACK_PAWN)
            mgEval += F6_PAWN_SHIELD;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_F] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}

        if (EvalBoard->squares[BB_G7] == BLACK_PAWN || EvalBoard->squares[BB_G6] == BLACK_PAWN)
            ;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_G] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}

        if (EvalBoard->squares[BB_H7] == BLACK_PAWN || EvalBoard->squares[BB_H6] == BLACK_PAWN)
            ;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_H] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}
	}
    else if (col <= FILE_C)
    {
        if (EvalBoard->squares[BB_C7] == BLACK_PAWN)
            ;
        else if (EvalBoard->squares[BB_C6] == BLACK_PAWN)
            mgEval += C6_PAWN_SHIELD;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_C] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}

        if (EvalBoard->squares[BB_B7] == BLACK_PAWN || EvalBoard->squares[BB_B6] == BLACK_PAWN)
            ;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_B] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}


        if (EvalBoard->squares[BB_A7] == BLACK_PAWN || EvalBoard->squares[BB_A6] == BLACK_PAWN)
            ;
        else
		{
            mgEval += NO_PAWN_SHIELD;
			if (FileMask[FILE_A] & EvalBoard->bbPieces[PAWN][WHITE])
				mgEval += OPPOSITE_PAWN;
		}
	}
	else
		mgEval += KING_IN_CENTER;

    // now check for opponent pieces attacking the vicinity of the king
    squares = bbKingMoves[bksq] | EvalBoard->bbPieces[KING][BLACK];

    while (squares)
    {
        int attacks, king_sq_attacks;
        BOOL bKingSq = FALSE;
        int square = BitScan(PopLSB(&squares));

        king_sq_attacks = 0;
        if (square == bksq)
            bKingSq = TRUE;

        // queen attacks
		if (EvalBoard->bbPieces[QUEEN][WHITE])
		{
			attacks = BitCount(EvalBoard->bbPieces[QUEEN][WHITE] & bbDiagonalMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval += attacks * Q_ATTACKER;
			attacks = BitCount(EvalBoard->bbPieces[QUEEN][WHITE] & bbStraightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval += attacks * Q_ATTACKER;
		}

        // rook attacks
		if (EvalBoard->bbPieces[ROOK][WHITE])
		{
			attacks = BitCount(EvalBoard->bbPieces[ROOK][WHITE] & bbStraightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval += attacks * R_ATTACKER;
		}

        // bishop attacks
		if (EvalBoard->bbPieces[BISHOP][WHITE])
		{
			attacks = BitCount(EvalBoard->bbPieces[BISHOP][WHITE] & bbDiagonalMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval += attacks * B_ATTACKER;
		}

        // knight attacks
		if (EvalBoard->bbPieces[KNIGHT][WHITE])
		{
			attacks = BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] & bbKnightMoves[square]);
			if (bKingSq)
				king_sq_attacks += attacks;
			mgEval += attacks * N_ATTACKER;
		}

        if (bKingSq)
            mgEval += king_sq_attacks * KING_SQ_ATTACKER;
    }

    if (bEval)
        printf("Eval = %d/%d after king safety\n", mgEval, egEval);

    // perform tapered eval calculation according to current board phase, then add to initial eval (which was just material imbalance)
    nEval = GetTaperedEval(EvalBoard->phase, mgEval, egEval);

#if USE_MATERIAL_IMBALANCE
	if ((wWood > bWood) && (bWood > 0) && (wWood < QUEEN_VAL))
		nEval *= (float)((float)wWood / bWood);
	else if ((bWood > wWood) && (wWood > 0) && (bWood < QUEEN_VAL))
		nEval *= (float)((float)bWood / wWood);
#endif

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
    if ((nEval > 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][WHITE] | EvalBoard->bbPieces[BISHOP][WHITE]) == 1) &&
        ((EvalBoard->bbMaterial[WHITE]) == (EvalBoard->bbPieces[KNIGHT][WHITE] | EvalBoard->bbPieces[BISHOP][WHITE] | EvalBoard->bbPieces[KING][WHITE])))
    {
        nEval = 0;
        goto exit;
    }
    if ((nEval < 0) && (BitCount(EvalBoard->bbPieces[KNIGHT][BLACK] | EvalBoard->bbPieces[BISHOP][BLACK]) == 1) &&
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
    if (nEval > nBeta)
        return(nBeta);
    return(nEval);
}

/*========================================================================
** FastEvaluate - evaluation based on material and PST only, also checks for material draw
**========================================================================
*/
int FastEvaluate(BB_BOARD* Board)
{
	int	sq, ptsq;
	int	piece, mult, color;
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

#if USE_INCREMENTAL_PST
	mgEval = EvalBoard->mgPST;
	egEval = EvalBoard->egPST;
#else
	mgEval = egEval = 0;

	// eval all wood 
	for (piece = KING; piece <= PAWN; piece++)
	{
		for (color = WHITE; color <= BLACK; color++)
		{
			Bitboard pieces = EvalBoard->bbPieces[piece][color];

			if (pieces == 0)
				continue;

			while (pieces)
			{
				ptsq = sq = BitScan(PopLSB(&pieces));

				if (color == BLACK)
					ptsq = VFlipSquare[ptsq];	// piece square table conversion

				// get piece info
				mult = (color == WHITE ? 1 : -1);

				// piece square table lookup
				mgEval += PST[piece][ptsq] * mult;
				egEval += PST[piece + 6][ptsq] * mult;
			}
		}
	} // piece counting loop
#endif

	nEval = GetTaperedEval(EvalBoard->phase, mgEval, egEval);
	if (EvalBoard->sidetomove == BLACK)
		nEval = -nEval;
	return(nEval);
}

/*========================================================================
** EPDTune - run through EPD file, collect static eval scores and produce
** overall "accuracy" score via sigmoid function
**========================================================================
*/
double EPDTune(char* filename, double K)
{
    char    pos[128];
    int     n, eval;
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
        BBForsytheToBoard(pos, &bbBoard);
        eval = BBEvaluate(&bbBoard, -INFINITY, INFINITY);
        if (bbBoard.sidetomove == BLACK)
            eval *= -1;

        // get game result
        if (strstr(pos, "1-0"))
            R = 1.0;
        else if (strstr(pos, "1.0"))
            R = 1.0;
        else if (strstr(pos, "1/2"))
            R = 0.5;
        else if (strstr(pos, "0.5"))
            R = 0.5;
        else
            R = 0.0;

        // compute sigmoid
        sigmoid = (1 / (1 + pow(10, (-K * eval / 400.0))));
		if (sigmoid <= 0)
			sigmoid = 0.00000001;
		if (sigmoid >= 1)
			sigmoid = 0.99999999;
        E += pow(R - sigmoid, 2);

        n++;
    }

    fclose(epd);
    return(E);
}
