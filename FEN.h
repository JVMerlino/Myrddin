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

#define ISNUMBER(x) (x >= '0' && x <= '9')

extern char	ePieceLabel[NPIECES];
extern char *ForsytheSymbols;

char *BBSquareName(SquareType square, char *buffer);

int BBForsytheToBoard(char *forsythe_str, BB_BOARD *Board, int *starting_move_number);
char *BBBoardToForsythe(BB_BOARD *Board, int move_number, char *buffer);
