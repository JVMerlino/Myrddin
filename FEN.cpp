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

#include <ctype.h>
#include "Myrddin.h"
#include "Bitboards.h"
#include "Fen.h"
#include "MoveGen.h"
#include "Eval.h"

char	ePieceLabel[NPIECES] = {'K', 'Q', 'R', 'B', 'N', 'P'};
char	*ForsytheSymbols = "KQRBNP";

/*========================================================================
** eStrCatC - cats character to string
**========================================================================
*/
char *StrCatC(char *buffer, char ch)
{
    buffer[strlen(buffer) + 1] = '\0';
    buffer[strlen(buffer)] = ch;
    return buffer;
}

/*========================================================================
** eSquareName - converts a SQUARE to coordinate notation
**========================================================================
*/
char *BBSquareName(SquareType square, char *buffer)
{
    strcpy(buffer, "  ");
    buffer[0] = 'a' + (square & 7);
    buffer[1] = '8' - (square >> 3);

    return buffer;
}

/*========================================================================
** CheckCastleLegality -- Sets that CASTLE_STATUS flag on a board
**========================================================================
*/
void BBCheckCastleLegality(BB_BOARD *Board)
{
    Board->castles = 0;

    if (Board->squares[BB_E1] == WHITE_KING)
    {
        if (Board->squares[BB_H1] == WHITE_ROOK)
            Board->castles |= WHITE_KINGSIDE_BIT;

        if (Board->squares[BB_A1] == WHITE_ROOK)
            Board->castles |= WHITE_QUEENSIDE_BIT;
    }

    if (Board->squares[BB_E8] == BLACK_KING)
    {
        if (Board->squares[BB_H8] == BLACK_ROOK)
            Board->castles |= BLACK_KINGSIDE_BIT;

        if (Board->squares[BB_A8] == BLACK_ROOK)
            Board->castles |= BLACK_QUEENSIDE_BIT;
    }
}

/*========================================================================
** eCharacterizeBoard -- Sets various flags on a board
**========================================================================
*/
void BBCharacterizeBoard(BB_BOARD *Board)
{
    BBCheckCastleLegality(Board);
    Board->epSquare = NO_EN_PASSANT;
    Board->inCheck = FALSE;
}

/*========================================================================
** ForsytheToBoard - Converts a FEN string to a board
**========================================================================
*/
int BBForsytheToBoard(char *forsythe_str, BB_BOARD *Board)
{
    int			row = 0;
    int			col = 0;
    char*		search = forsythe_str;
    char*		search_ch;
    char        temp_str[256];
    PieceType   temp_castle_status, piece;
    ColorType	color;

    strcpy(temp_str, forsythe_str);

    ZeroMemory(Board, sizeof(BB_BOARD));

    search = strtok(temp_str, " ");

    while (*search)
    {
        if (ISNUMBER(*search))
        {
            if (ISNUMBER(*(search + 1)))
            {
                row += (10 * (*search - '0') + (*(search + 1) - '0')) / 8 - 1;
                ++search;
            }
            else
                col += (*search - '0');
        }
        else if (*search == '/')
        {
            ++row;
            col = 0;
        }
        else
        {
            int	square;

            if (row >= BSIZE || col >= BSIZE)
                return -1;

            search_ch = strchr(ForsytheSymbols, toupper(*search));
            if (search_ch == NULL)
                return -1;

            square = (row * 8) + col;
            piece = (PieceType)(search_ch - ForsytheSymbols);
            color = ((*search == toupper(*search)) ? WHITE : BLACK);

            Board->squares[square] = piece | ((color == WHITE) ? XWHITE : XBLACK);
			IS_PIECE_OK(piece);

            switch (piece)
            {
				case KING:
					SetBit(&Board->bbPieces[KING][color], square);
					break;
				case QUEEN:
					SetBit(&Board->bbPieces[QUEEN][color], square);
					break;
				case ROOK:
					SetBit(&Board->bbPieces[ROOK][color], square);
					break;
				case BISHOP:
					SetBit(&Board->bbPieces[BISHOP][color], square);
					break;
				case KNIGHT:
					SetBit(&Board->bbPieces[KNIGHT][color], square);
					break;
				case PAWN:
					SetBit(&Board->bbPieces[PAWN][color], square);
					break;
            }

            ++col;
        }

        ++search;
    }

    Board->bbMaterial[WHITE] = Board->bbPieces[KING][WHITE] | Board->bbPieces[QUEEN][WHITE] | Board->bbPieces[ROOK][WHITE] |
                               Board->bbPieces[BISHOP][WHITE] | Board->bbPieces[KNIGHT][WHITE] | Board->bbPieces[PAWN][WHITE];
    Board->bbMaterial[BLACK] = Board->bbPieces[KING][BLACK] | Board->bbPieces[QUEEN][BLACK] | Board->bbPieces[ROOK][BLACK] |
                               Board->bbPieces[BISHOP][BLACK] | Board->bbPieces[KNIGHT][BLACK] | Board->bbPieces[PAWN][BLACK];
    Board->bbOccupancy = Board->bbMaterial[WHITE] | Board->bbMaterial[BLACK];

    BBCharacterizeBoard(Board);	// checks for castle legality
    Board->sidetomove = WHITE;

    // look for color to move
    search = strtok(NULL, " ");
    if (search == NULL)
        return 0;

    if (*search == 'w' || *search == 'W')
        Board->sidetomove = WHITE;
    else if (*search == 'b' || *search == 'B')
        Board->sidetomove = BLACK;
    else
        return -1;

    // look for castling capability
    search = strtok(NULL, " ");
    if (search == NULL)
        return 0;

    temp_castle_status = 0;
    while (*search != '\0' && *search != '-')
    {
        switch (*search)
        {
			case 'K':
				temp_castle_status |= WHITE_KINGSIDE_BIT;
				break;

			case 'Q':
				temp_castle_status |= WHITE_QUEENSIDE_BIT;
				break;

			case 'k':
				temp_castle_status |= BLACK_KINGSIDE_BIT;
				break;

			case 'q':
				temp_castle_status |= BLACK_QUEENSIDE_BIT;
				break;
        }

        ++search;
    }

    Board->castles = temp_castle_status;

    // look for en passant target square
    search = strtok(NULL, " ");
    if (search == NULL)
        return 0;

    if (*search != '-')
    {
        if (*search < 'a' || *search > 'h')
            return -1;
        if (*(search + 1) != '3' && *(search + 1) != '6')
            return -1;
        if (*(search + 2) != '\0')
            return -1;

        Board->epSquare = (('8' - *(search + 1)) * 8) + (*search - 'a');
        Board->epSquare += (Board->sidetomove == WHITE ? 8 : -8);
    }

    // currently we ignore half move
    search = strtok(NULL, " ");
    if (search == NULL)
        return 0;

    Board->fifty = (PieceType)atoi(search);
    if (Board->fifty < 0)
        Board->fifty = 0;

    // look for starting move number
    search = strtok(NULL, " ");
    if (search == NULL)
        return 0;

    return 0;
}

/*========================================================================
** BoardToForsythe - Converts a board to a FEN string
**========================================================================
*/
char *BBBoardToForsythe(BB_BOARD *Board, int move_number, char *buffer)
{
    int	row, col;
    int  empty_count;
    int  sq;
    char ch;

    *buffer = 0;
    empty_count = 0;

    for (row = 0; row < BSIZE; ++row)
    {
        for (col = 0; col < BSIZE; ++col)
        {
            sq = ((row * 8) + col);
            if (Board->squares[sq] == EMPTY)
                ++empty_count;
            else
            {
                if (empty_count > 0)
                    StrCatC(buffer, (char)('0' + empty_count));
                empty_count = 0;
                ch = ePieceLabel[PIECEOF(Board->squares[sq])];
                if (COLOROF(Board->squares[sq]) == XBLACK)
                    ch = (char)tolower((char)ch);
                StrCatC(buffer, ch);
            }
        }

        if (empty_count > 0)
            StrCatC(buffer, (char)('0' + empty_count));
        empty_count = 0;
        if (row < BSIZE - 1)
            strcat(buffer, "/");
    }

    // export start color
    strcat(buffer, (Board->sidetomove == WHITE) ? " w" : " b");

    // export castling avail
    if (Board->castles == 0)
        strcat(buffer, " -");
    else
    {
        strcat(buffer, " ");
        if (Board->castles & WHITE_KINGSIDE_BIT)
            strcat(buffer, "K");
        if (Board->castles & WHITE_QUEENSIDE_BIT)
            strcat(buffer, "Q");
        if (Board->castles & BLACK_KINGSIDE_BIT)
            strcat(buffer, "k");
        if (Board->castles & BLACK_QUEENSIDE_BIT)
            strcat(buffer, "q");
    }

    // export en passant pawn
    if (Board->epSquare == NO_EN_PASSANT)
        strcat(buffer, " -");
    else
    {
        strcat(buffer, " ");
        if (COLOROF(Board->squares[Board->epSquare]) == XWHITE)
            BBSquareName((SquareType)(Board->epSquare + 8), buffer + strlen(buffer));
        else
            BBSquareName((SquareType)(Board->epSquare - 8), buffer + strlen(buffer));
    }

    // we don't support half moves, but we do support initial move
    sprintf(buffer + strlen(buffer), " 0 %d", move_number);

    return buffer;
}
