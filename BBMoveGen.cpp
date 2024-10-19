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

#include <intrin.h>

#include "Myrddin.h"
#include "bitboards.h"
#include "magicmoves.h"
#include "MoveGen.h"
#include "Eval.h"
#include "Hash.h"
#include "parray.inc"
#include "FEN.h"
#include "cerebrum.h"

#if USE_INCREMENTAL_ACC_UPDATE
NN_Accumulator	AccStack[MAX_DEPTH];
int				AccStackIndex = 0;
#endif

#if VERIFY_BOARD
/*========================================================================
** VerifyWood -- Verifies that nTotalWood is correct
**========================================================================
*/
BOOL VerifyWood(BB_BOARD *Board)
{
#if 0   // rewrite?
	int	nWood[2];
	int	piece, color;

	nWood[0] = nWood[1] = 0;
	for (color = WHITE; color <= BLACK; color++)
	{
		for (piece = QUEEN; piece <= PAWN; piece++)
		{
			nWood[color] += (BitCount(Board->bbPieces[piece][color]) * nPieceVals[piece]);
		}
	}

	return((nTotalWood[WHITE] == nWood[WHITE]) && (nTotalWood[BLACK] == nWood[BLACK]));
#endif
    return(TRUE);
}
#endif

#if 0
/*========================================================================
** GetAllAttackers -- Returns bitboard of all pieces (both colors) attacking
** square 'sq'. Includes pinned piece attacks. This may include kings
** capturing into check and does *not* include ep captures. Used for SEE
**========================================================================
*/
Bitboard GetAllAttackers(BB_BOARD *Board, int square)
{
    IS_SQ_OK(square);

    Bitboard attackers = 0;

    // find all possible enemy attackers
	INDEX_CHECK(square, bbKnightMoves);
    attackers |= bbKnightMoves[square] & (Board->bbPieces[KNIGHT][WHITE] | Board->bbPieces[KNIGHT][BLACK]);
	INDEX_CHECK(square, bbKingMoves);
    attackers |= bbKingMoves[square] & (Board->bbPieces[KING][WHITE] | Board->bbPieces[KING][BLACK]);
    attackers |= Bmagic(square, Board->bbOccupancy) & (Board->bbPieces[BISHOP][WHITE] | Board->bbPieces[QUEEN][WHITE] | Board->bbPieces[BISHOP][BLACK] | Board->bbPieces[QUEEN][BLACK]);
    attackers |= Rmagic(square, Board->bbOccupancy) & (Board->bbPieces[ROOK][WHITE] | Board->bbPieces[QUEEN][WHITE] | Board->bbPieces[ROOK][BLACK] | Board->bbPieces[QUEEN][BLACK]);
    attackers |= (bbPawnAttacks[WHITE][square] & Board->bbPieces[PAWN][WHITE]) | (bbPawnAttacks[BLACK][square] & Board->bbPieces[PAWN][BLACK]);

    return(attackers);
}
#endif

/*========================================================================
** GetAttackers -- Returns bitboard of all enemy pieces attacking square 'sq'
** includes pinned piece attacks, also used as in-check function. This may
** include kings capturing into check and does *not* include ep captures
**========================================================================
*/
Bitboard GetAttackers(BB_BOARD *Board, int square, int color, BOOL bNeedOnlyOne)
{
    IS_SQ_OK(square);
    IS_COLOR_OK(color);

    Bitboard attackers = 0;

    // find all possible enemy attackers
	INDEX_CHECK(square, bbKnightMoves);
    attackers = bbKnightMoves[square] & Board->bbPieces[KNIGHT][color];
    if (attackers && bNeedOnlyOne)
        return(attackers);
	INDEX_CHECK(square, bbKingMoves);
    attackers |= bbKingMoves[square] & Board->bbPieces[KING][color];
    if (attackers && bNeedOnlyOne)
        return(attackers);
    attackers |= Bmagic(square, Board->bbOccupancy) & (Board->bbPieces[BISHOP][color] | Board->bbPieces[QUEEN][color]);
    if (attackers && bNeedOnlyOne)
        return(attackers);
    attackers |= Rmagic(square, Board->bbOccupancy) & (Board->bbPieces[ROOK][color] | Board->bbPieces[QUEEN][color]);
    if (attackers && bNeedOnlyOne)
        return(attackers);
    attackers |= bbPawnAttacks[color][square] & Board->bbPieces[PAWN][color];

    return(attackers);
}

/*========================================================================
** KingInDanger - Is the King in check
**========================================================================
*/
BOOL  BBKingInDanger(BB_BOARD *Board, int color)
{
    IS_COLOR_OK(color);

    if (GetAttackers(Board, BitScan(Board->bbPieces[KING][color]), OPPONENT(color), TRUE))
        return(TRUE);
    else
        return(FALSE);
}

/*========================================================================
** BBAddToMoveList -- Adds a CHESSMOVE to a move list
**========================================================================
*/
void BBAddToMoveList(CHESSMOVE *legal_move_list, WORD *next_move, SquareType from_square, SquareType to_square,
                     MoveFlagType moveflag, int score)
{
    assert(*next_move >= 0 && *next_move < MAX_LEGAL_MOVES);
    assert(legal_move_list);
    IS_SQ_OK(from_square);
    IS_SQ_OK(to_square);
    assert(score >= 0);

    legal_move_list[*next_move].fsquare = from_square;
    legal_move_list[*next_move].tsquare = to_square;
    legal_move_list[*next_move].moveflag = moveflag;
    legal_move_list[*next_move].nScore = score;
    (*next_move)++;
}

/*========================================================================
** Score Capture -- do MVV/LVA scoring of capture
**========================================================================
*/
int	BBScoreCapture(PieceType Capturer, PieceType Captured)
{
    IS_PIECE_OK(Capturer);
    assert(Captured >= QUEEN && Captured <= PAWN);	// don't allow king captures? IS_PIECE_OK(Captured);

    return(CAPTURE_SORT_VAL + (nPieceVals[Captured] * 16) - nPieceVals[Capturer]);
}

/*========================================================================
** GenerateNormalMoves -- all moves for non-pawns, except castling
**========================================================================
*/
void BBGenerateNormalMoves(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *next_move, int color,
                           BOOL CapturesOnly)
{
    IS_COLOR_OK(color);
    assert(legal_move_list);
    assert(*next_move >= 0 && *next_move <= MAX_LEGAL_MOVES);

    Bitboard	moves, target, pieces;
    DWORD		dest;
    int			score = 0, piecetype;
    int			opp = OPPONENT(color);
    BOOL		capture;

    for (piecetype = KING; piecetype < PAWN; piecetype++)
    {
        pieces = Board->bbPieces[piecetype][color];

        while (pieces)
        {
            int	square;

            Bitboard	piece = PopLSB(&pieces);
            square = BitScan(piece);

            // get all normal moves for any piece
            switch (piecetype)
            {
				case BISHOP:
					moves = Bmagic(square, Board->bbOccupancy);
					break;
				case ROOK:
					moves = Rmagic(square, Board->bbOccupancy);
					break;
				case QUEEN:
					moves = Qmagic(square, Board->bbOccupancy);
					break;
				case KNIGHT:
					moves = bbKnightMoves[square];
					break;
				case KING:
					moves = bbKingMoves[square];
					break;
            }

            // mask out moves that capture piece of same color
            moves &= ~Board->bbMaterial[color];

            // if captures only, get rid of non-capture moves
            if (CapturesOnly)
                moves &= Board->bbMaterial[opp];

            while (moves)
            {
                target = PopLSB(&moves);
                dest = BitScan(target);
                capture = ((target & Board->bbMaterial[opp]) > 0);

                if (capture && (PIECEOF(Board->squares[dest]) == KING))	// a bit of a hack, as this situation can occur when verifying checkmate
                    continue;

                // do move scoring of captures
                if (capture)
                    score = BBScoreCapture((PieceType)piecetype, PIECEOF(Board->squares[dest]));
                else
                    score = 0;

                BBAddToMoveList(legal_move_list, next_move, (SquareType)square, (SquareType)dest, (MoveFlagType)(capture ? MOVE_CAPTURE : 0), score);
            }
        }
    }
}

/*========================================================================
** GenerateCastles -- generates legal castles only!
**========================================================================
*/
void BBGenerateCastles(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *next_move, int color)
{
    assert(legal_move_list);
    IS_COLOR_OK(color);
    assert(*next_move >= 0 && *next_move <= MAX_LEGAL_MOVES);

    int	opp = OPPONENT(color);
    int	castles = Board->castles;

    if (color == WHITE)
    {
        // kingside castling is legal
        if (castles & WHITE_KINGSIDE_BIT)
        {
            // castling squares are empty
            if ((Board->bbOccupancy & wkc) == EMPTY)
            {
                // nobody attacking castling squares
                if ((GetAttackers(Board, BB_F1, opp, TRUE) | (GetAttackers(Board, BB_G1, opp, TRUE))) == 0)
                    BBAddToMoveList(legal_move_list, next_move, BB_E1, BB_G1, MOVE_OO, 0);
            }
        }

        // queenside castling is legal
        if (castles & WHITE_QUEENSIDE_BIT)
        {
            // castling squares are empty
            if ((Board->bbOccupancy & wqc) == EMPTY)
            {
                // nobody attacking castling squares
                if ((GetAttackers(Board, BB_D1, opp, TRUE) | (GetAttackers(Board, BB_C1, opp, TRUE))) == 0)
                    BBAddToMoveList(legal_move_list, next_move, BB_E1, BB_C1, MOVE_OOO, 0);
            }
        }
    }
    else	// color == BLACK
    {
        // kingside castling is legal
        if (castles & BLACK_KINGSIDE_BIT)
        {
            // castling squares are empty
            if ((Board->bbOccupancy & bkc) == EMPTY)
            {
                // nobody attacking castling squares
                if ((GetAttackers(Board, BB_F8, opp, TRUE) | (GetAttackers(Board, BB_G8, opp, TRUE))) == 0)
                    BBAddToMoveList(legal_move_list, next_move, BB_E8, BB_G8, MOVE_OO, 0);
            }
        }

        // queenside castling is legal
        if (castles & BLACK_QUEENSIDE_BIT)
        {
            // castling squares are empty
            if ((Board->bbOccupancy & bqc) == EMPTY)
            {
                // nobody attacking castling squares
                if ((GetAttackers(Board, BB_D8, opp, TRUE) | (GetAttackers(Board, BB_C8, opp, TRUE))) == 0)
                    BBAddToMoveList(legal_move_list, next_move, BB_E8, BB_C8, MOVE_OOO, 0);
            }
        }
    }
}

/*========================================================================
** GeneratePawnMoves -- all moves for pawns, including promotions and en passant
**========================================================================
*/
void BBGeneratePawnMoves(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *next_move, int color,
                         BOOL CapturesOnly)
{
    assert(legal_move_list);
    IS_COLOR_OK(color);
	assert(*next_move >= 0 && *next_move <= MAX_LEGAL_MOVES);

    Bitboard		moves, target;
    Bitboard		capture = 0;
    Bitboard		pieces = Board->bbPieces[PAWN][color];
    int				dest;
    int				score = 0;
    int				opp = OPPONENT(color);
    MoveFlagType	flag;

    while (pieces)
    {
        Bitboard piece = PopLSB(&pieces);
        int square = BitScan(piece);
		IS_SQ_OK(square);
        moves = bbPawnMoves[color][square];

        // mask out moves that capture piece of same color
        moves &= ~Board->bbMaterial[color];

        // if captures only, mask out all moves that go forward unless they are promotions
        if (CapturesOnly)
        {
            moves &= ~FileMask[File(square)];
            moves |= (bbPawnMoves[color][square] & (BB_RANK_1 | BB_RANK_8));
        }

        while (moves)
        {
            flag = 0;

            target = PopLSB(&moves);
            dest = BitScan(target);
            score = 0;

            // check for pawn capture move
            if ((dest - square) & 1)
            {
                // ensure there is a piece there to be captured
                capture = (target & Board->bbMaterial[opp]);

                // if not a normal capture, might be en passant
                if (!capture && (dest == (color == WHITE ? Board->epSquare - 8 : Board->epSquare + 8)))
                {
                    if (PIECEOF(Board->squares[Board->epSquare]) != PAWN)
                        continue;

                    flag |= (MOVE_ENPASSANT | MOVE_CAPTURE);
                    score = BBScoreCapture(PAWN, PAWN);
                    capture = TRUE;
                }

                if (!capture)
                    continue;

                if (capture && ((flag & MOVE_ENPASSANT) == 0) && (PIECEOF(Board->squares[dest]) == KING))	// a bit of a hack, as this situation can occur when verifying checkmate
                    continue;

                // do move scoring of normal pawn captures, as en passant captures have score of 0 (pawn takes pawn)
                if ((flag & MOVE_ENPASSANT) == 0)
                    score = BBScoreCapture(PAWN, PIECEOF(Board->squares[dest]));

                flag |= MOVE_CAPTURE;
            }
            else
            {
                // pawn is moving forward, make sure no other piece is there or is in between in the case of a two-square move
                if (target & Board->bbOccupancy)
                    continue;

                if (abs(dest - square) == 16)
                {
                    if (Bit[(dest + square) / 2] & Board->bbOccupancy)
                        continue;
                }
            }

            // add moves to list, including all promotion moves
            if (target & (BB_RANK_8 | BB_RANK_1))	// check for promotion
            {
                PieceType	promoted;

                for (promoted = FIRST_PROMOTE; promoted <= LAST_PROMOTE; promoted++)
                    BBAddToMoveList(legal_move_list, next_move, (SquareType)square, (SquareType)dest, (MoveFlagType)(flag | promoted | MOVE_PROMOTED), score);
            }
            else
                BBAddToMoveList(legal_move_list, next_move, (SquareType)square, (SquareType)dest, flag, score);
        }
    }
}

/*========================================================================
** GenerateAllMoves -- Generates all moves, verifying that they are legal
** and also flagging moves that give check
**========================================================================
*/
void BBGenerateAllMoves(BB_BOARD *Board, CHESSMOVE *legal_move_list, WORD *total_moves, BOOL CapturesOnly)
{
    int	x, kingsquare;
    WORD pseudo_moves = 0;
    int color = Board->sidetomove;

    *total_moves = 0;

#if VERIFY_BOARD
    BB_BOARD	BoardTemp;
    memcpy(&BoardTemp, Board, sizeof(BB_BOARD));
#endif

    BBGenerateNormalMoves(Board, legal_move_list, &pseudo_moves, color, CapturesOnly);

    if (Board->castles && !Board->inCheck && !CapturesOnly)
        BBGenerateCastles(Board, legal_move_list, &pseudo_moves, color);	// generates legal castles only!

    if (Board->bbPieces[PAWN][color])
        BBGeneratePawnMoves(Board, legal_move_list, &pseudo_moves, color, CapturesOnly);

    // now verify legality of moves
    kingsquare = BitScan(Board->bbPieces[KING][color]);

    for (x = 0; x < pseudo_moves; x++)
    {
        CHESSMOVE	move;
        int	from, to, flag;
        int newkingsquare;
        int frompiece, topiece;

        move = legal_move_list[x];
        from = move.fsquare;
        to = move.tsquare;
        flag = move.moveflag;

        frompiece = Board->squares[from];
        topiece = Board->squares[to];

        // adjust the bitboards
		if (topiece)
			RemovePiece(Board, to, FALSE);
		MovePiece(Board, from, to, FALSE);

        // has king moved?
        if (PIECEOF(frompiece) == KING)
            newkingsquare = to;
        else
            newkingsquare = kingsquare;

        if (flag & MOVE_ENPASSANT)
            RemovePiece(Board, Board->epSquare, FALSE);
        else if (flag & MOVE_OO)
			MovePiece(Board, (color == WHITE ? BB_H1 : BB_H8), (color == WHITE ? BB_F1 : BB_F8), FALSE);
        else if (flag & MOVE_OOO)
			MovePiece(Board, (color == WHITE ? BB_A1 : BB_A8), (color == WHITE ? BB_D1 : BB_D8), FALSE);
        else if (flag & MOVE_PROMOTED)
        {
            RemovePiece(Board, to, FALSE);
            PutPiece(Board, (color == WHITE ? XWHITE | flag & MOVE_PIECEMASK : XBLACK | flag & MOVE_PIECEMASK), to, FALSE);
        }

        if (GetAttackers(Board, newkingsquare, OPPONENT(color), TRUE))	// move left king in check, so it is illegal
            legal_move_list[x].moveflag |= MOVE_REJECTED;

        // put the bitboards back
		MovePiece(Board, to, from, FALSE);
        if (topiece)
            PutPiece(Board, topiece, to, FALSE);

        if (flag & MOVE_ENPASSANT)
            PutPiece(Board, PAWN | (color == WHITE ? XBLACK : XWHITE), Board->epSquare, FALSE);
        else if (flag & MOVE_OO)
			MovePiece(Board, (color == WHITE ? BB_F1 : BB_F8), (color == WHITE ? BB_H1 : BB_H8), FALSE);
        else if (flag & MOVE_OOO)
			MovePiece(Board, (color == WHITE ? BB_D1 : BB_D8), (color == WHITE ? BB_A1 : BB_A8), FALSE);
        else if (flag & MOVE_PROMOTED)
        {
            RemovePiece(Board, from, FALSE);
            PutPiece(Board, (color == WHITE ? WHITE_PAWN : BLACK_PAWN), from, FALSE);
        }
    }

#if VERIFY_BOARD
    assert(memcmp(&BoardTemp, Board, sizeof(BB_BOARD)) == 0);
#endif

    // now remove all illegal moves
    WORD next_good_move = 0;

    for (x = 0; x < pseudo_moves; ++x)
    {
        if (!(legal_move_list[x].moveflag & MOVE_REJECTED))
        {
            if (next_good_move != x)
                legal_move_list[next_good_move] = legal_move_list[x];

            ++next_good_move;
        }
    }

    *total_moves = next_good_move;
}

/*========================================================================
** UpdateCastleStatus - Called after every makemove if any castles are legal
**========================================================================
*/
void BBUpdateCastleStatus(BB_BOARD *Board, SquareType from, SquareType to)
{
    int	castles = Board->castles;

    if (from == BB_E1)
        castles &= ~(WHITE_QUEENSIDE_BIT | WHITE_KINGSIDE_BIT);
    if (from == BB_E8)
        castles &= ~(BLACK_QUEENSIDE_BIT | BLACK_KINGSIDE_BIT);

    if (castles & WHITE_QUEENSIDE_BIT)
    {
        if ((from == BB_A1) || (to == BB_A1))
            castles &= ~WHITE_QUEENSIDE_BIT;
    }

    if (castles & WHITE_KINGSIDE_BIT)
    {
        if ((from == BB_H1) || (to == BB_H1))
            castles &= ~WHITE_KINGSIDE_BIT;
    }

    if (castles & BLACK_QUEENSIDE_BIT)
    {
        if ((from == BB_A8) || (to == BB_A8))
            castles &= ~BLACK_QUEENSIDE_BIT;
    }

    if (castles & BLACK_KINGSIDE_BIT)
    {
        if ((from == BB_H8) || (to == BB_H8))
            castles &= ~BLACK_KINGSIDE_BIT;
    }

    Board->castles = castles;
}

/*========================================================================
** MakeMove - makes move and returns captured piece if any
**========================================================================
*/
void BBMakeMove(CHESSMOVE* move_to_make, BB_BOARD* Board)
{
    assert(move_to_make);
    assert(Board);

    PUNDOMOVE		save_undo;
    PosSignature	dwSignature;
    BYTE			pFromIndex, pToIndex, pIndex;
    MoveFlagType	moveflag = move_to_make->moveflag;
    SquareType		from = move_to_make->fsquare;
    SquareType		to = move_to_make->tsquare;
    int      		moving_piece = Board->squares[from];
    int				captured_piece = Board->squares[to];
    ColorType		my_color = COLOROF(moving_piece);
	BOOL			bUpdateAcc = FALSE;

    assert(from != to);
    IS_SQ_OK(from);
    IS_SQ_OK(to);

    // save board information for unmaking move
    save_undo = &move_to_make->save_undo;

    save_undo->dwSignature = Board->signature;
    save_undo->castle_status = (BYTE)Board->castles;
    save_undo->en_passant_pawn = (SquareType)Board->epSquare;
    save_undo->in_check_status = (BYTE)Board->inCheck;
    save_undo->capture_square = to;
    save_undo->captured_piece = (PieceType)captured_piece;
    save_undo->fifty_move = (BYTE)Board->fifty;

#if USE_INCREMENTAL_ACC_UPDATE
	memcpy(&AccStack[AccStackIndex++], &accumulator, sizeof(NN_Accumulator));
	bUpdateAcc = TRUE;
#endif

    // fix the board signature -- other fixes may be necessary later in this function
    dwSignature = save_undo->dwSignature;

    // move piece to target square and remove captured piece on target square (if any)
    pFromIndex = PIECEOF(moving_piece);
    if (my_color == XBLACK)
        pFromIndex += 6;

    assert(pFromIndex >= 0 && pFromIndex < 12);

    dwSignature ^= aPArray[pFromIndex][from];
    dwSignature ^= aPArray[pFromIndex][to];

    if (captured_piece != EMPTY)
    {
        pToIndex = PIECEOF(captured_piece);
        if (COLOROF(captured_piece) == XBLACK)
            pToIndex += 6;
        assert(pToIndex >= 0 && pToIndex < 12);
        dwSignature ^= aPArray[pToIndex][to];
    }

    // undo en passant status
    if (Board->epSquare != NO_EN_PASSANT)
        dwSignature ^= aEPArray[Board->epSquare];

    // switch sides
    dwSignature ^= aSTMArray[WHITE];
    dwSignature ^= aSTMArray[BLACK];

    // 50-move counter
    if ((PIECEOF(moving_piece) == PAWN) || (captured_piece != EMPTY))
        Board->fifty = 0;
    else
        Board->fifty++;

    // move the pieces
	if (captured_piece != EMPTY)
		RemovePiece(Board, to, bUpdateAcc);
	MovePiece(Board, from, to, bUpdateAcc);

    if (moveflag & MOVE_ENPASSANT)
    {
        int cap_square = Board->epSquare;

        save_undo->capture_square = (SquareType)cap_square;
        captured_piece = (PAWN | OPPOSITE(my_color));
        save_undo->captured_piece = (PieceType)captured_piece;

        pIndex = PAWN;
        if (my_color == XWHITE)
            pIndex += 6;
        dwSignature ^= aPArray[pIndex][cap_square];

        RemovePiece(Board, cap_square, bUpdateAcc);
    }

    // check for initial pawn move and set en passant square
    Board->epSquare = NO_EN_PASSANT;
    if (PIECEOF(moving_piece) == PAWN)
    {
        if (abs(from - to) == 16)
        {
            Board->epSquare = to;
            dwSignature ^= aEPArray[to];
        }
    }

    // check for castling moves and update king square
    if (PIECEOF(moving_piece) == KING)
    {
        if (moveflag & MOVE_OO)
        {
            pIndex = ROOK;
            if (my_color == XBLACK)
                pIndex += 6;

            dwSignature ^= aPArray[pIndex][to + 1];
            dwSignature ^= aPArray[pIndex][from + 1];

			MovePiece(Board, to + 1, from + 1, bUpdateAcc);
        }
        else if (moveflag & MOVE_OOO)
        {
            pIndex = ROOK;
            if (my_color == XBLACK)
                pIndex += 6;

            dwSignature ^= aPArray[pIndex][to - 2];
            dwSignature ^= aPArray[pIndex][from - 1];

			MovePiece(Board, to - 2, from - 1, bUpdateAcc);
        }
    }

    // update castle status
    if (Board->castles)
    {
        dwSignature ^= aCSArray[Board->castles];
        BBUpdateCastleStatus(Board, from, to);
        dwSignature ^= aCSArray[Board->castles];
    }

    if (moveflag & MOVE_PROMOTED)
    {
        pIndex = PAWN;
        if (my_color == XBLACK)
            pIndex += 6;
        dwSignature ^= aPArray[pIndex][to];

        pIndex = (BYTE)moveflag & MOVE_PIECEMASK;
        if (my_color == XBLACK)
            pIndex += 6;
        dwSignature ^= aPArray[pIndex][to];

        RemovePiece(Board, to, bUpdateAcc);
        PutPiece(Board, my_color | (moveflag & MOVE_PIECEMASK), to, bUpdateAcc);
    }

    Board->inCheck = BBKingInDanger(Board, OPPONENT(Board->sidetomove));
    if (Board->inCheck)
        move_to_make->moveflag |= MOVE_CHECK;

    Board->sidetomove = OPPONENT(Board->sidetomove);

    Board->signature = dwSignature;

#if VERIFY_BOARD
    assert(Board->signature == GetBBSignature(Board));
	assert(VerifyWood(Board));
#endif
}

/*========================================================================
** eUnMakeMove - Takes back a move from a board
**========================================================================
*/
void BBUnMakeMove(CHESSMOVE *move_to_unmake, BB_BOARD *Board)
{
    assert(move_to_unmake);
    assert(Board);

    PUNDOMOVE 	save_undo;
    SquareType  from = move_to_unmake->fsquare;
    SquareType	to = move_to_unmake->tsquare;
    ColorType   which_color = COLOROF(Board->squares[to]);

    save_undo = &move_to_unmake->save_undo;

#if USE_INCREMENTAL_ACC_UPDATE
	memcpy(&accumulator, AccStack[--AccStackIndex], sizeof(NN_Accumulator));
#endif

	MovePiece(Board, to, from, FALSE);
    if (save_undo->captured_piece)
        PutPiece(Board, save_undo->captured_piece, save_undo->capture_square, FALSE);

    Board->castles = save_undo->castle_status;
    Board->epSquare = save_undo->en_passant_pawn;
    Board->inCheck = save_undo->in_check_status;
    Board->fifty = save_undo->fifty_move;
    Board->signature = save_undo->dwSignature;

    if (move_to_unmake->moveflag & MOVE_PROMOTED)
    {
        RemovePiece(Board, from, FALSE);
        PutPiece(Board, which_color | PAWN, from, FALSE);
    }

    if (move_to_unmake->moveflag & MOVE_OO)
    {
        if (COLOROF(Board->squares[from]) == XWHITE)
			MovePiece(Board, BB_F1, BB_H1, FALSE);
        else
			MovePiece(Board, BB_F8, BB_H8, FALSE);
    }
    else if (move_to_unmake->moveflag & MOVE_OOO)
    {
        if (COLOROF(Board->squares[from]) == XWHITE)
			MovePiece(Board, BB_D1, BB_A1, FALSE);
        else
			MovePiece(Board, BB_D8, BB_A8, FALSE);
    }

    Board->sidetomove = OPPONENT(Board->sidetomove);
#if VERIFY_BOARD
	assert(VerifyWood(Board));
#endif
}

/*========================================================================
** MakeNullMove - makes null move
**========================================================================
*/
void	BBMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board)
{
    PUNDOMOVE    save_undo;

    save_undo = &cmNull->save_undo;

    save_undo->dwSignature = Board->signature;
    save_undo->castle_status = (BYTE)Board->castles;
    save_undo->en_passant_pawn = (SquareType)Board->epSquare;
    save_undo->in_check_status = (BYTE)Board->inCheck;
    save_undo->fifty_move = (BYTE)Board->fifty;

    Board->inCheck = FALSE;
    Board->fifty++;

    // update board signature
    PosSignature dwSignature = save_undo->dwSignature;

    // switch side to move
    dwSignature ^= aSTMArray[WHITE];
    dwSignature ^= aSTMArray[BLACK];
    Board->sidetomove = OPPONENT(Board->sidetomove);

    // undo en passant
    if (Board->epSquare != NO_EN_PASSANT)
        dwSignature ^= aEPArray[Board->epSquare];
    Board->epSquare = NO_EN_PASSANT;

    Board->signature = dwSignature;
}

/*========================================================================
** UnMakeNullMove - unmakes null move
**========================================================================
*/
void	BBUnMakeNullMove(CHESSMOVE *cmNull, BB_BOARD *Board)
{
    PUNDOMOVE    save_undo;

    save_undo = &cmNull->save_undo;

    Board->castles = save_undo->castle_status;
    Board->epSquare = save_undo->en_passant_pawn;
    Board->inCheck = save_undo->in_check_status;
    Board->fifty = save_undo->fifty_move;
    Board->signature = save_undo->dwSignature;
    Board->sidetomove = OPPONENT(Board->sidetomove);
}
