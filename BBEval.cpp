/* 
Myrddin XBoard / WinBoard compatible chess engine written in C
Copyright(C) 2024  John Merlino

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
#include "MoveGen.h"

#if USE_CEREBRUM_1_0
#include "cerebrum 1-0.h"
#else
#include "cerebrum 2-0.h"
#endif

#if USE_EGTB
#include "TBProbe.h"
#endif

int		nEval;

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

#if USE_EGTB
    if (!tb_available)  // if tablebases are not available, check for material draw
#endif
    {
        int nTotalPieces = BitCount(EvalBoard->bbOccupancy);

        if (nTotalPieces == 2)
        {
            nEval = 0;	// just kings on board
            goto exit;
        }

        if ((nTotalPieces == 3) && (BitCount(EvalBoard->bbPieces[BISHOP][WHITE] | EvalBoard->bbPieces[BISHOP][BLACK] | EvalBoard->bbPieces[KNIGHT][WHITE] | EvalBoard->bbPieces[KNIGHT][BLACK]) == 1))
        {
            nEval = 0;	// king and minor vs king
            goto exit;
        }

        if (nTotalPieces == 4)
        {
            if ((BitCount(EvalBoard->bbPieces[BISHOP][WHITE] | EvalBoard->bbPieces[KNIGHT][WHITE]) == 1) && (BitCount(EvalBoard->bbPieces[BISHOP][BLACK] | EvalBoard->bbPieces[KNIGHT][BLACK]) == 1))
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
    }

#if !USE_INCREMENTAL_ACC_UPDATE
	nn_update_all_pieces(EvalBoard->Accumulator, EvalBoard->bbPieces);
#endif

	nEval = nn_evaluate(EvalBoard->Accumulator, EvalBoard->sidetomove);

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
