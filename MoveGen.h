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

////////////////////////////////////////////////////////////////////////////////
// MoveFlag values
#define MOVE_PIECEMASK  0x0007  // promoted piece, if any   -----------+
#define MOVE_ENPASSANT  0x0008  // en passant               ----------+|
#define MOVE_OOO        0x0010  // castle queen side (O-O-O)---------+||
#define MOVE_OO         0x0020  // castle king  side (O-O)  --------+|||
#define MOVE_PROMOTED   0x0040  // promotion                -------+||||
#define MOVE_CHECK      0x0080  // move causes check        ------+|||||
#define MOVE_CAPTURE    0x0100  // move captures a piece    -----+||||||
#define MOVE_REJECTED   0x0200  // move rejected because    ----+|||||||
								// it leaves or moves king      ||||||||
								// in check                     ||||||||
#define MOVE_CHECKMATE  0x0400  // move causes checkmate    ---+||||||||
#define MOVE_NULL       0x0800  // move is null move        --+|||||||||
#define	MOVE_SEARCHED	0x1000	// move has been searched   -+||||||||||	// not in use (yet)
								//                           snmrccpkqeppp

#define MOVE_NOT_QUIET	0x01F0	// castle, promotion, check, or capture

#define  FIRST_PROMOTE  (QUEEN)
#define  LAST_PROMOTE   (KNIGHT)

// move scoring values
#define  HASH_SORT_VAL		0x500000
#define  CAPTURE_SORT_VAL	0x200000

#define  KILLER_1_SORT_VAL  0x200003	// this places killer moves after good captures, but before equal or losing captures
#define  KILLER_2_SORT_VAL  0x200002
#define  KILLER_3_SORT_VAL  0x200001
#define  MATE_KILLER_BONUS  0x010000

#define	 MAX_HISTORY_VAL	0x0FFFFF

void			BBGenerateAllMoves(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *next_move, BOOL CapturesOnly);
int 			BBKingInDanger(BB_BOARD *Board, int whose_king);
void     		BBMakeMove(CHESSMOVE *move_to_make, BB_BOARD *Board);
void			BBUnMakeMove(CHESSMOVE *move_to_unmake, BB_BOARD *Board);
void			BBMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board);
void			BBUnMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board);
Bitboard		GetAttackers(BB_BOARD *Board, int square, int color, BOOL bNeedOnlyOne);
//Bitboard		GetAllAttackers(BB_BOARD *Board, int square);
