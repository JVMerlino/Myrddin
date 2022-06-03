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

#include <intrin.h>
#include "magicmoves.h"
#include "Myrddin.h"
#include "Bitboards.h"
#include "FEN.h"

BOOL		bPopcnt = FALSE;

Bitboard	bbPawnMoves[2][64];
Bitboard	bbPawnAttacks[2][64];
Bitboard	bbKnightMoves[64];
Bitboard	bbKingMoves[64];
Bitboard	bbPassedPawnMask[2][64];
Bitboard	bbDiagonalMoves[64];
Bitboard	bbStraightMoves[64];
Bitboard	Bit[64];

Bitboard RankMask[8] =
{
    BB_RANK_8, BB_RANK_7, BB_RANK_6, BB_RANK_5, BB_RANK_4, BB_RANK_3, BB_RANK_2, BB_RANK_1
};

Bitboard FileMask[8] =
{
    BB_FILE_A, BB_FILE_B, BB_FILE_C, BB_FILE_D, BB_FILE_E, BB_FILE_F, BB_FILE_G, BB_FILE_H
};

BB_BOARD	bbBoard;

int RemovePiece(BB_BOARD *Board, int square)
{
    IS_SQ_OK(square);
    assert(Board->squares[square]);

    int	piece = Board->squares[square];
    int color = COLOROF(piece) == XWHITE ? WHITE : BLACK;

    Board->squares[square] = EMPTY;
    ClearBit(&Board->bbPieces[PIECEOF(piece)][color], square);
    ClearBit(&Board->bbMaterial[color], square);
    ClearBit(&Board->bbOccupancy, square);

    return(piece);
}

void PutPiece(BB_BOARD *Board, int piece, int square)
{
    IS_SQ_OK(square);
    assert(Board->squares[square] == EMPTY);

    int color = COLOROF(piece) == XWHITE ? WHITE : BLACK;

    Board->squares[square] = piece;
    SetBit(&Board->bbPieces[PIECEOF(piece)][color], square);
    SetBit(&Board->bbMaterial[color], square);
    SetBit(&Board->bbOccupancy, square);
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
    bPopcnt = (CPUInfo[2] & 0x800000);
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

    // passed pawn mask -- all squares in which an enemy pawn must exist for a pawn on a particular square to NOT be passed
    // white passers
    // first, fill the mask with all squares from the pawn's file, and any adjacent files
    // then mask out all squares in the pawn's rank and all squares in all ranks behind the pawn
    for (sq = 0; sq <= 63; sq++)
    {
        int rank;

        bbPassedPawnMask[WHITE][sq] = 0;

        if (File(sq) != FILE_A)
            bbPassedPawnMask[WHITE][sq] |= FileMask[File(sq) - 1];
        bbPassedPawnMask[WHITE][sq] |= FileMask[File(sq)];
        if (File(sq) != FILE_H)
            bbPassedPawnMask[WHITE][sq] |= FileMask[File(sq) + 1];

        for (rank = Rank(sq); rank <= RANK_1; rank++)
            bbPassedPawnMask[WHITE][sq] &= ~RankMask[rank];
    }
    // black passers
    for (sq = 0; sq <= 63; sq++)
    {
        int rank;

        bbPassedPawnMask[BLACK][sq] = 0;

        if (File(sq) != FILE_A)
            bbPassedPawnMask[BLACK][sq] |= FileMask[File(sq) - 1];
        bbPassedPawnMask[BLACK][sq] |= FileMask[File(sq)];
        if (File(sq) != FILE_H)
            bbPassedPawnMask[BLACK][sq] |= FileMask[File(sq) + 1];

        for (rank = Rank(sq); rank >= RANK_8; rank--)
            bbPassedPawnMask[BLACK][sq] &= ~RankMask[rank];
    }

    // straight attacks
    for (sq = 0; sq <= 63; sq++)
        bbStraightMoves[sq] = (RankMask[Rank(sq)] | FileMask[File(sq)]) & ~Bit[sq];

    // diagonal attacks
    for (sq = 0; sq <= 63; sq++)
        bbDiagonalMoves[sq] = Bmagic(sq, 0);
}
