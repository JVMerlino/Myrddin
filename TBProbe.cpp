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

#include "Myrddin.h"
#include "Bitboards.h"
#include "gtb-probe.h"

int tb_available = FALSE;			/* 0 => FALSE, 1 => TRUE */

#if USE_EGTB

char **paths;	/* paths where TB files will be searched */
int	stm;				/* side to move */
int	epsquare;			/* target square for an en passant capture */
int	castling;			/* castling availability, 0 => no castles */
unsigned int  ws[17];	/* list of squares for white */
unsigned int  bs[17];	/* list of squares for black */
unsigned char wp[17];	/* what white pieces are on those squares */
unsigned char bp[17];	/* what black pieces are on those squares */
unsigned info = tb_UNKNOWN;	/* default, no tbvalue */
unsigned pliestomate;

/*========================================================================
** GavtiotaTBProbe - Probes the Gaviota TBs if less than five pieces
**========================================================================
*/
int GaviotaTBProbe(BB_BOARD *Board, BOOL bProbeSoft)
{
    int	nRow, nCol;
    int nWPiece, nBPiece;
    int nPiece, nColor;
    SquareType nSquare;
    BOOL bSuccess;

    if (!tb_available)
        return(-1);

    stm      = (Board->sidetomove == WHITE ? tb_WHITE_TO_MOVE : tb_BLACK_TO_MOVE);	/* 0 = white to move, 1 = black to move */
    castling = tb_NOCASTLE;		/* no castling available */
    nWPiece = nBPiece = 0;

    if (Board->epSquare != NO_EN_PASSANT)
    {
        nRow = Rank(Board->epSquare);

        epsquare = tb_A1 + ((7 - nRow) << 3) + File(Board->epSquare);
        epsquare += (Board->sidetomove == WHITE ? 8 : -8);
    }
    else
        epsquare = tb_NOSQUARE;		/* no ep available */

    // fill up probe piece array
    for (nSquare = 0; nSquare < 64; nSquare++)
    {
        nRow = Rank(nSquare);
        nCol = File(nSquare);

        if (Board->squares[nSquare] == EMPTY)
            continue;

        nPiece = PIECEOF(Board->squares[nSquare]);
        nColor = COLOROF(Board->squares[nSquare]);

        if (nColor == XWHITE)
        {
            ws[nWPiece] = tb_A1 + ((7 - nRow) << 3) + nCol;
            wp[nWPiece] = (unsigned char)(tb_KING - nPiece);
            nWPiece++;
        }
        else
        {
            bs[nBPiece] = tb_A1 + ((7 - nRow) << 3) + nCol;
            bp[nBPiece] = (unsigned char)(tb_KING - nPiece);
            nBPiece++;
        }
    }

    ws[nWPiece] = tb_NOSQUARE;
    wp[nWPiece] = tb_NOPIECE;
    bs[nBPiece] = tb_NOSQUARE;
    bp[nBPiece] = tb_NOPIECE;

    if (bProbeSoft)
        bSuccess = tb_probe_soft(stm, epsquare, castling, ws, bs, wp, bp, &info, &pliestomate);
    else
        bSuccess = tb_probe_hard(stm, epsquare, castling, ws, bs, wp, bp, &info, &pliestomate);

#if 0 // _DEBUG
    if (bLog)
    {
        char buffer[128];

        if (bSuccess)
            fprintf(logfile, "TBProbe %s -> %d, %d\n", BoardToForsythe(ptEvalBoard, nEvalSideToMove, 0, buffer), info, pliestomate);
        else
            fprintf(logfile, "TBProbe %s -> FAILS, soft=%d\n", BoardToForsythe(ptEvalBoard, nEvalSideToMove, 0, buffer), bProbeSoft);
    }
#endif

    if (bSuccess)
    {
        if (info == tb_DRAW)
            return(0);
        else if (info == tb_WMATE && stm == tb_WHITE_TO_MOVE)
            return(CHECKMATE - pliestomate);
        else if (info == tb_BMATE && stm == tb_BLACK_TO_MOVE)
            return(CHECKMATE - pliestomate);
        else if (info == tb_WMATE && stm == tb_BLACK_TO_MOVE)
            return(-CHECKMATE + pliestomate);
        else if (info == tb_BMATE && stm == tb_WHITE_TO_MOVE)
            return(-CHECKMATE + pliestomate);
        else {
            printf ("#FATAL ERROR, This should never be reached\n");
            return(EXIT_FAILURE);
        }
    }
    else
        return(EXIT_FAILURE);
}

/*========================================================================
** GaviotaTBInit - initialize Gaviota Tablebases
**========================================================================
*/
int GaviotaTBInit(void)
{
    size_t cache_size = 32*1024*1024; /* 32 MiB in this example */
    int verbosity = 0;		/* initialization 0 = non-verbose, 1 = verbose */
    int	scheme = nEGTBCompressionType;	/* compression scheme to be used */

    paths = (char **) tbpaths_init();
    paths = (char **) tbpaths_add((char **)paths, szEGTBPath);

    tb_init (verbosity, scheme, (char **) paths);

    tbcache_init(cache_size);

    tbstats_reset();

    /*--------------------------------------*\
    |   Probing info to provide
    \*--------------------------------------*/

    int	stm;				/* side to move */
    int	epsquare;			/* target square for an en passant capture */
    int	castling;			/* castling availability, 0 => no castles */
    unsigned int  ws[17];	/* list of squares for white */
    unsigned int  bs[17];	/* list of squares for black */
    unsigned char wp[17];	/* what white pieces are on those squares */
    unsigned char bp[17];	/* what black pieces are on those squares */
    int nTests;

    /*--------------------------------------*\
    |   Probing info requested
    \*--------------------------------------*/

    unsigned info = tb_UNKNOWN;	/* default, no tbvalue */
    unsigned pliestomate;

    /* do a test probe to make sure they are there -- test for 3, 4 and 5 pieces */

    for (nTests = 0; nTests <= 2; nTests++)
    {
        stm      = tb_WHITE_TO_MOVE;/* 0 = white to move, 1 = black to move */
        epsquare = tb_NOSQUARE;		/* no ep available */
        castling = tb_NOCASTLE;		/* no castling available */

        if (nTests == 0)
        {
//			printf("\n# 3-man (KPK) test -- 4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\n");
            if (bLog)
                fprintf(logfile, "\n3-man test -- 4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\n");

            ws[0] = tb_E1;
            ws[1] = tb_E2;
            ws[3] = tb_NOSQUARE;		/* it marks the end of list */

            wp[0] = tb_KING;
            wp[1] = tb_PAWN;
            wp[2] = tb_NOPIECE;			/* it marks the end of list */

            bs[0] = tb_E8;
            bs[1] = tb_NOSQUARE;		/* it marks the end of list */

            bp[0] = tb_KING;
            bp[1] = tb_NOPIECE;			/* it marks the end of list */
        }
        else if (nTests == 1)
        {
            if (bLog)
                fprintf(logfile, "4-man test -- 8/8/8/p7/1k6/8/4P3/4K3 b - - 0 1\n");

            stm      = tb_BLACK_TO_MOVE;/* 0 = white to move, 1 = black to move */

            ws[0] = tb_E1;
            ws[1] = tb_E2;
            ws[2] = tb_NOSQUARE;		/* it marks the end of list */

            wp[0] = tb_KING;
            wp[1] = tb_PAWN;
            wp[2] = tb_NOPIECE;			/* it marks the end of list */

            bs[0] = tb_B4;
            bs[1] = tb_A5;
            bs[2] = tb_NOSQUARE;		/* it marks the end of list */

            bp[0] = tb_KING;
            bp[1] = tb_PAWN;
            bp[2] = tb_NOPIECE;			/* it marks the end of list */
        }
        else if (nTests == 2)
        {
            if (bLog)
                fprintf(logfile, "5-man test -- 8/3p4/3k4/8/8/8/2P1P3/4K3 b - - 0 1\n");

            stm      = tb_BLACK_TO_MOVE;/* 0 = white to move, 1 = black to move */

            ws[0] = tb_E1;
            ws[1] = tb_E2;
            ws[2] = tb_C2;
            ws[3] = tb_NOSQUARE;		/* it marks the end of list */

            wp[0] = tb_KING;
            wp[1] = tb_PAWN;
            wp[2] = tb_PAWN;
            wp[3] = tb_NOPIECE;			/* it marks the end of list */

            bs[0] = tb_D6;
            bs[1] = tb_D7;
            bs[2] = tb_NOSQUARE;		/* it marks the end of list */

            bp[0] = tb_KING;
            bp[1] = tb_PAWN;
            bp[2] = tb_NOPIECE;			/* it marks the end of list */
        }

        tb_available = tb_probe_hard (stm, epsquare, castling, ws, bs, wp, bp, &info, &pliestomate);

        if (tb_available)
        {
            if (bLog)
            {
                fprintf(logfile, "Gaviota Tablebases found, sample position shows ");
                if (info == tb_DRAW)
                    fprintf(logfile, "Draw\n");
                else if (info == tb_WMATE && stm == tb_WHITE_TO_MOVE)
                    fprintf(logfile, "White mates, plies=%u\n", pliestomate);
                else if (info == tb_BMATE && stm == tb_BLACK_TO_MOVE)
                    fprintf(logfile, "Black mates, plies=%u\n", pliestomate);
                else if (info == tb_WMATE && stm == tb_BLACK_TO_MOVE)
                    fprintf(logfile, "Black is mated, plies=%u\n", pliestomate);
                else if (info == tb_BMATE && stm == tb_WHITE_TO_MOVE)
                    fprintf(logfile, "White is mated, plies=%u\n", pliestomate);
                else {
                    fprintf(logfile, "FATAL ERROR, This should never be reached\n");
                    exit(EXIT_FAILURE);
                }
                fprintf(logfile, "\n");
            }
        }
        else
        {
            if (bLog)
                fprintf(logfile, "Tablebase info not available\n\n");
        }
    }

    /*--------------------------------------*\
    |         		Return
    \*--------------------------------------*/

    if (tb_available)
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}

/*========================================================================
** GaviotaTBClose - close Gaviota Tablebases
**========================================================================
*/
void GaviotaTBClose(void)
{
    tbcache_done();

    tb_done();

    paths = (char **) tbpaths_done((char **)paths);
}

#endif	// USE_EGTB