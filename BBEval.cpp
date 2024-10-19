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
#include "Hash.h"
#include "TBProbe.h"
#include "MoveGen.h"
#include "cerebrum.h"

int		nEval;

NN_Accumulator accumulator;
#define NN_FILE		"myrddin.nn"
#define NN_TO_CP	512		// multiplication factor to convert NN return value to centipawns - David's value was 1000, but the scores seemed too high

/*========================================================================
** Evaluate - assign a "goodness" score to the current position on the
** eval board
**========================================================================
*/
int BBEvaluate(BB_BOARD *EvalBoard, int nAlpha, int nBeta)
{
#if USE_EVAL_HASH
    EVAL_HASH_ENTRY *found = ProbeEvalHash(EvalBoard->signature);
    if (found)
    {
        if (found->nEval <= nAlpha)
            return(nAlpha);
        if (found->nEval >= nBeta)
            return(nBeta);
        return(found->nEval);
    }
#endif

#if 0
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
#endif

#if !USE_INCREMENTAL_ACC_UPDATE
	nn_update_all_pieces(accumulator, EvalBoard->bbPieces);
#endif
	nEval = (int)(nn_evaluate(accumulator, EvalBoard->sidetomove) * NN_TO_CP);

exit:
#if USE_EVAL_HASH
    SaveEvalHash(nEval, EvalBoard->signature);
#endif

    if (nEval <= nAlpha)
        return(nAlpha);
    if (nEval >= nBeta)
        return(nBeta);
    return(nEval);
}
