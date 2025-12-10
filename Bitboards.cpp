/*
Myrddin XBoard / WinBoard compatible chess engine written in C
Copyright(C) 2024 John Merlino

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

#include <intrin.h>
#include "magicmoves.h"
#include "Myrddin.h"
#include "Bitboards.h"
#include "Eval.h"

#if USE_CEREBRUM_1_0
#include "cerebrum.h"
#else
#include "cerebrum 2-0.h"
#endif

extern NN_Accumulator accumulator;

BOOL		bPopcnt = FALSE;

Bitboard	bbPawnMoves[2][64];
Bitboard	bbPawnAttacks[2][64];
Bitboard	bbKnightMoves[64];
Bitboard	bbKingMoves[64];
Bitboard	Bit[64];
Bitboard	wkc, wqc, bkc, bqc;

// Bitboard	bbDiagonalMoves[64];
// Bitboard	bbStraightMoves[64];
// Bitboard	bbBetween[8][8];

const Bitboard RankMask[8] =
{
    BB_RANK_8, BB_RANK_7, BB_RANK_6, BB_RANK_5, BB_RANK_4, BB_RANK_3, BB_RANK_2, BB_RANK_1
};

const Bitboard FileMask[8] =
{
    BB_FILE_A, BB_FILE_B, BB_FILE_C, BB_FILE_D, BB_FILE_E, BB_FILE_F, BB_FILE_G, BB_FILE_H
};

// int nPiecePhaseVals[NPIECES] = { 0, QUEEN_PHASE, ROOK_PHASE, MINOR_PHASE, MINOR_PHASE, PAWN_PHASE };

#if 0
int VFlipSquare[64] = {
	BB_A1, BB_B1, BB_C1, BB_D1, BB_E1, BB_F1, BB_G1, BB_H1,
	BB_A2, BB_B2, BB_C2, BB_D2, BB_E2, BB_F2, BB_G2, BB_H2,
	BB_A3, BB_B3, BB_C3, BB_D3, BB_E3, BB_F3, BB_G3, BB_H3,
	BB_A4, BB_B4, BB_C4, BB_D4, BB_E4, BB_F4, BB_G4, BB_H4,
	BB_A5, BB_B5, BB_C5, BB_D5, BB_E5, BB_F5, BB_G5, BB_H5,
	BB_A6, BB_B6, BB_C6, BB_D6, BB_E6, BB_F6, BB_G6, BB_H6,
	BB_A7, BB_B7, BB_C7, BB_D7, BB_E7, BB_F7, BB_G7, BB_H7,
	BB_A8, BB_B8, BB_C8, BB_D8, BB_E8, BB_F8, BB_G8, BB_H8,
};
#endif

BB_BOARD	bbBoard;

void RemovePiece(BB_BOARD* Board, int square, BOOL bUpdateNN)
{
	IS_SQ_OK(square);
	assert(Board->squares[square]);

	int	piece = Board->squares[square];
	int color = COLOROF(piece) == XWHITE ? WHITE : BLACK;
	int pstpiece = PIECEOF(piece);

	Board->squares[square] = EMPTY;
	ClearBit(&Board->bbPieces[pstpiece][color], square);
	ClearBit(&Board->bbMaterial[color], square);
	ClearBit(&Board->bbOccupancy, square);

#if USE_INCREMENTAL_ACC_UPDATE
	if (bUpdateNN)
#if USE_CEREBRUM_1_0
		nn_del_piece(Board->Accumulator, pstpiece, color, square ^ 56);
#else
		nn_del_piece(Board->Accumulator, 5 - pstpiece, color, square ^ 56);
#endif
#endif
}

void PutPiece(BB_BOARD *Board, int piece, int square, BOOL bUpdateNN)
{
    IS_SQ_OK(square);
	assert(Board->squares[square] == EMPTY);

    int color = COLOROF(piece) == XWHITE ? WHITE : BLACK;
	int pstpiece = PIECEOF(piece);

    Board->squares[square] = piece;
    SetBit(&Board->bbPieces[pstpiece][color], square);
    SetBit(&Board->bbMaterial[color], square);
    SetBit(&Board->bbOccupancy, square);

#if USE_INCREMENTAL_ACC_UPDATE
	if (bUpdateNN)
#if USE_CEREBRUM_1_0
		nn_add_piece(Board->Accumulator, pstpiece, color, square ^ 56);
#else
		nn_add_piece(Board->Accumulator, 5 - pstpiece, color, square ^ 56);
#endif
#endif
}

void MovePiece(BB_BOARD* Board, int from, int to, BOOL bUpdateNN)
{
	IS_SQ_OK(from);
	IS_SQ_OK(to);

	int	piece = Board->squares[from];
	int color = COLOROF(piece) == XWHITE ? WHITE : BLACK;
	int pstpiece = PIECEOF(piece);

	Board->squares[from] = EMPTY;
	Board->squares[to] = piece;

	ClearBit(&Board->bbPieces[pstpiece][color], from);
	ClearBit(&Board->bbMaterial[color], from);
	ClearBit(&Board->bbOccupancy, from);
	SetBit(&Board->bbPieces[pstpiece][color], to);
	SetBit(&Board->bbMaterial[color], to);
	SetBit(&Board->bbOccupancy, to);

#if USE_INCREMENTAL_ACC_UPDATE
	if (bUpdateNN)
#if USE_CEREBRUM_1_0
		nn_mov_piece(Board->Accumulator, pstpiece, color, from ^ 56, to ^ 56);
#else
		nn_mov_piece(Board->Accumulator, 5 - pstpiece, color, from ^ 56, to ^ 56);
#endif
#endif
}

void initbitboards(void)
{
    int	sq;

    // bit array
    for (sq = 0; sq <= 63; sq++)
        Bit[sq] = (0x01ULL << sq);

#ifdef _WIN64
    // check for 64-bit popcnt support
    int CPUInfo[4] = {-1};
    __cpuid(CPUInfo, 1);
    bPopcnt = CPUInfo[2] & 0x800000;
#endif

    // takes care of all queen, rook and bishop moves -- thanks, Pradu!
    initmagicmoves();

    // knight moves
    for (sq = 0; sq <= 63; sq++)
    {
        bbKnightMoves[sq] = 0;

        // up 2, left 1
        if ((sq % 8) && (sq > 15))
		{
			assert(sq-17 >= 0 && sq-17 < 64);
            SetBit(&bbKnightMoves[sq], sq - 17);
		}
        // up 2, right 1
        if ((sq % 8 < 7) && (sq > 15))
		{
			assert(sq-15 >= 0 && sq-15 < 64);
            SetBit(&bbKnightMoves[sq], sq - 15);
		}

        // up 1, left 2
        if ((sq % 8 > 1) && (sq > 7))
		{
			assert(sq-10 >= 0 && sq-10 < 64);
			SetBit(&bbKnightMoves[sq], sq - 10);
		}

        // up 1, right 2
        if ((sq % 8 < 6) && (sq > 7))
		{
			assert(sq-6 >= 0 && sq-6 < 64);
            SetBit(&bbKnightMoves[sq], sq - 6);
		}

        // down 2, left 1
        if ((sq % 8) && (sq < 48))
		{
			assert(sq + 15 >= 0 && sq + 15 < 64);
            SetBit(&bbKnightMoves[sq], sq + 15);
		}

        // down 2, right 1
        if ((sq % 8 < 7) && (sq < 48))
		{
			assert(sq + 17 >= 0 && sq + 17 < 64);
            SetBit(&bbKnightMoves[sq], sq + 17);
		}

        // down 1, left 2
        if ((sq % 8 > 1) && (sq < 56))
		{
			assert(sq + 6 >= 0 && sq + 6 < 64);
            SetBit(&bbKnightMoves[sq], sq + 6);
		}

        // down 1, right 2
        if ((sq % 8 < 6) && (sq < 56))
		{
			assert(sq + 10 >= 0 && sq + 10 < 64);
            SetBit(&bbKnightMoves[sq], sq + 10);
		}
    }

    // king moves
    for (sq = 0; sq <= 63; sq++)
    {
        bbKingMoves[sq] = 0;

        // up 1, left 1
        if ((sq % 8) && (sq > 7))
		{
			assert(sq - 9 >= 0 && sq - 9 < 64);
            SetBit(&bbKingMoves[sq], sq - 9);
		}

        // up 1
        if (sq > 7)
		{
			assert(sq - 8 >= 0 && sq - 8 < 64);
            SetBit(&bbKingMoves[sq], sq - 8);
		}

        // up 1, right 1
        if ((sq % 8 < 7) && (sq > 7))
		{
			assert(sq - 7 >= 0 && sq - 7 < 64);
            SetBit(&bbKingMoves[sq], sq - 7);
		}

        // left 1
        if (sq % 8)
		{
			assert(sq - 1 >= 0 && sq - 1 < 64);
            SetBit(&bbKingMoves[sq], sq - 1);
		}

        // right 1
        if (sq % 8 < 7)
		{
			assert(sq + 1 >= 0 && sq + 1 < 64);
            SetBit(&bbKingMoves[sq], sq + 1);
		}

        // down 1, left 1
        if ((sq % 8) && (sq < 56))
		{
			assert(sq + 7 >= 0 && sq + 7 < 64);
            SetBit(&bbKingMoves[sq], sq + 7);
		}

        // down 1
        if (sq < 56)
		{
			assert(sq + 8 >= 0 && sq + 8 < 64);
            SetBit(&bbKingMoves[sq], sq + 8);
		}

        // down 1, right 1
        if ((sq % 8 < 7) && (sq < 56))
		{
			assert(sq + 9 >= 0 && sq + 9 < 64);
            SetBit(&bbKingMoves[sq], sq + 9);
		}
    }

    // initialize pawn attacks arrays
    for (sq = 0; sq <= 63; sq++)
    {
        bbPawnAttacks[WHITE][sq] = 0;
        bbPawnAttacks[BLACK][sq] = 0;
    }

    // white pawn moves (including en passant and two-square initial moves, which are culled later if illegal)
    for (sq = 0; sq <= 63; sq++)
    {
        bbPawnMoves[WHITE][sq] = 0;

        // forward one
        if ((sq < 56) && (sq > 7))
		{
			assert(sq - 8 >= 0 && sq - 8 < 64);
            SetBit(&bbPawnMoves[WHITE][sq], sq - 8);
		}

        // forward two
        if ((sq >= 48) && (sq < 56))
		{
			assert(sq - 16 >= 0 && sq - 16 < 64);
            SetBit(&bbPawnMoves[WHITE][sq], sq - 16);
		}

        // capture to left
        if ((sq % 8) && (sq < 56) && (sq > 7))
        {
			assert(sq - 9 >= 0 && sq - 9 < 64);
            SetBit(&bbPawnMoves[WHITE][sq], sq - 9);
            SetBit(&bbPawnAttacks[WHITE][sq - 9], sq);
        }

        // capture to right
        if ((sq % 8 < 7) && (sq < 56) && (sq > 7))
        {
			assert(sq - 7 >= 0 && sq - 7 < 64);
            SetBit(&bbPawnMoves[WHITE][sq], sq - 7);
            SetBit(&bbPawnAttacks[WHITE][sq - 7], sq);
        }
    }

    // black pawn moves (including en passant and two-square initial moves, which are culled later if illegal)
    for (sq = 0; sq <= 63; sq++)
    {
        bbPawnMoves[BLACK][sq] = 0;

        // forward one
        if ((sq < 56) && (sq > 7))
		{
			assert(sq + 8 >= 0 && sq + 8 < 64);
            SetBit(&bbPawnMoves[BLACK][sq], sq + 8);
		}

        // forward two
        if ((sq > 7) && (sq <= 15))
		{
			assert(sq + 16 >= 0 && sq + 16 < 64);
            SetBit(&bbPawnMoves[BLACK][sq], sq + 16);
		}

        // capture to left
        if ((sq % 8) && (sq < 56) && (sq > 7))
        {
			assert(sq + 7 >= 0 && sq + 7 < 64);
            SetBit(&bbPawnMoves[BLACK][sq], sq + 7);
            SetBit(&bbPawnAttacks[BLACK][sq + 7], sq);
        }

        // capture to right
        if ((sq % 8 < 7) && (sq < 56) && (sq > 7))
        {
			assert(sq + 9 >= 0 && sq + 9 < 64);
            SetBit(&bbPawnMoves[BLACK][sq], sq + 9);
            SetBit(&bbPawnAttacks[BLACK][sq + 9], sq);
        }
    }

#if 0
	// straight attacks
    for (sq = 0; sq <= 63; sq++)
        bbStraightMoves[sq] = (RankMask[Rank(sq)] | FileMask[File(sq)]) & ~Bit[sq];

	// diagonal attacks
    for (sq = 0; sq <= 63; sq++)
        bbDiagonalMoves[sq] = Bmagic(sq, 0);

	int x, y;

	// bits between squares on a rank - used for determining FRC castling legality
	for (x = 0; x <= 6; x++)
	{
		for (y = x+1; y <= 7; y++)
		{
			if (abs(x - y) <= 1)
			{
				bbBetween[x][y] = BB_EMPTY;
				continue;
			}

			if (y < x)
			{
				sq = y + 1;
				while (sq < x)
					SetBit(&bbBetween[x][y], sq++);
			}
			else
			{
				sq = x + 1;
				while (sq < y)
					SetBit(&bbBetween[x][y], sq++);
			}
		}
	}
#endif

	// masks for checking castling squares
	wkc = Bit[BB_F1] | Bit[BB_G1];
	wqc = Bit[BB_B1] | Bit[BB_C1] | Bit[BB_D1];
	bkc = Bit[BB_F8] | Bit[BB_G8];
	bqc = Bit[BB_B8] | Bit[BB_C8] | Bit[BB_D8];
}
