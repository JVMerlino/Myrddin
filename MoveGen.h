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

////////////////////////////////////////////////////////////////////////////////
// MoveFlag values
#define  MOVE_PIECEMASK  0x0007  // promoted piece, if any   -----------+
#define  MOVE_ENPASSANT  0x0008  // en passant               ----------+|
#define  MOVE_OOO        0x0010  // castle queen side (O-O-O)---------+||
#define  MOVE_OO         0x0020  // castle king  side (O-O)  --------+|||
#define  MOVE_PROMOTED   0x0040  // promotion                -------+||||
#define  MOVE_CHECK      0x0080  // move causes check        ------+|||||
#define  MOVE_CAPTURE    0x0100  // move captures a piece    -----+||||||
#define  MOVE_REJECTED   0x0200  // move rejected because    ----+|||||||
								 // it leaves or moves king      ||||||||
								 // in check                     ||||||||
#define  MOVE_CHECKMATE  0x0400  // move causes checkmate    ---+||||||||
#define  MOVE_NULL       0x0800  // move is null move        --+|||||||||
								 //                            nmrccpkqeppp
#define  MOVE_ILLEGAL    0xFFFF  // Move not found in legal move list

#define  FIRST_PROMOTE  (QUEEN)
#define  LAST_PROMOTE   (KNIGHT)

// move scoring values
#define  PV_SORT_VAL		0x800000
#define  HASH_SORT_VAL		0x500000
#define  CAPTURE_SORT_VAL	0x200000
// #define  CHECK_SORT_VAL     0x100000

#define  KILLER_1_SORT_VAL  0x200002	// this places killer moves after good captures, but before equal or losing captures
#define  KILLER_2_SORT_VAL  0x200001
#define  KILLER_3_SORT_VAL  0x200000
#define  MATE_KILLER_BONUS  0 // 0x010000

#define	 MAX_HISTORY_VAL	0x0FFFFF

#define  TOTAL_PHASE		256	// uses 10 points for each knight/bishop, 22 points for each rook, 44 points for each queen - pawns don't count
#define  PAWN_PHASE			0
#define	 MINOR_PHASE		10
#define  ROOK_PHASE         22
#define  QUEEN_PHASE		44

void			BBGenerateAllMoves(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *next_move, BOOL CapturesOnly);
int 			BBKingInDanger(BB_BOARD *Board, int whose_king);
PieceType		BBMakeMove(CHESSMOVE *move_to_make, BB_BOARD *Board);
void			BBUnMakeMove(CHESSMOVE *move_to_unmake, BB_BOARD *Board);
void			BBMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board);
void			BBUnMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board);
Bitboard		GetAttackers(BB_BOARD *Board, int square, int color, BOOL bNeedOnlyOne);
Bitboard		GetAllAttackers(BB_BOARD *Board, int square);

extern int		nPiecePhaseVals[NPIECES];