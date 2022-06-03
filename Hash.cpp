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

#include <windows.h>
#include <intrin.h>
#include "Myrddin.h"
#include "Bitboards.h"
#include "Think.h"
#include "Hash.h"
#include "PArray.inc"

#define LOG_HASH		FALSE
#define ALWAYS_REPLACE	TRUE
#define USE_INTERLOCKED FALSE
#define HASH_AGING_FACTOR	4	// min number of plies difference between stored and incoming entry, including hash age

HASH_ENTRY		*HashTable = NULL;
PAWN_HASH_ENTRY *PawnHashTable = NULL;
EVAL_HASH_ENTRY *EvalHashTable = NULL;

size_t	dwHashSize = DEFAULT_HASH_SIZE; // results in 128MB of hash
size_t	dwPawnHashSize = 0x100000;		// results in 16MB of pawn hash
size_t	dwEvalHashSize = 0x200000;		// results in 32MB of eval hash

// short	nHashAge = 0;

int	nHashBails = 0;
int	nHashSaves = 0;
int	nHashHits = 0;
int	nHashProbes = 0;
int	nHashReturns = 0;
int	nHashZeroes = 0;

int nEvalHashHits = 0;
int nEvalHashSaves = 0;
int nEvalHashProbes = 0;
int nEvalHashBails = 0;

/*========================================================================
** ProbeHash - probes the transposition table for a matching entry
**========================================================================
*/
HASH_ENTRY *ProbeHash(PosSignature dwSignature)
{
    if (HashTable == NULL)
        return(NULL);

    PosSignature	index = dwSignature & (dwHashSize - 1);
    HASH_ENTRY		*pEntry = HashTable + index;

#if LOG_HASH
    if (bLog)
        nHashProbes++;
#endif

    if ((pEntry->h.dwSignature == dwSignature) /* && (pEntry->h.nDepth >= nDepth) */)
    {
        // verify that the position is identical and was searched to at least the appropriate depth
#if LOG_HASH
        if (bLog)
            nHashHits++;
#endif

        return pEntry;
    }
    else
    {
#if LOG_HASH
        if (bLog)
        {
            if (pEntry->h.nFlags == HASH_NOT_EVAL)
                nHashZeroes++;
        }
#endif
        return NULL;
    }
}

/*========================================================================
** SaveHash - saves a move in the hash table if applicable
**========================================================================
*/
void SaveHash(CHESSMOVE *cmMove, int nDepth, int nEval, BYTE nFlags, int nPly, PosSignature dwSignature)
{
    if (HashTable == NULL)
        return;

#if LOG_HASH
    if (bLog)
        nHashSaves++;
#endif

    PosSignature	index = dwSignature & (dwHashSize - 1);
    HASH_ENTRY		*pEntry = HashTable + index;

#if ALWAYS_REPLACE
    goto Replace;
#else
    // ALWAYS REPLACE if depth is equal or larger or if the data is for the same position
    if ((pEntry->h.nDepth <= nDepth) /* || pEntry->h.dwSignature == dwSignature */)
        goto Replace;
	else
		return;

#if 0
    // stored hash entry is exact (but is for a different position)
    // stored entry has no move and we have an incoming move
    if ((pEntry->h.from == NO_SQUARE) && cmMove)
        goto Replace;

    if (pEntry->nFlags & HASH_EXACT)
    {
        // replace only if stored entry is not within aging factor of incoming entry
        if ((pEntry->h.nAge + pEntry->h.nDepth + HASH_AGING_FACTOR) >= (nHashAge + nDepth))
        {
#if LOG_HASH
            if (bLog)
                nHashBails++;
#endif
            return;
        }
    }
    // stored hash entry is upper or lower bound
    else if ((pEntry->h.nFlags & (HASH_ALPHA | HASH_BETA)) && !(nFlags & HASH_EXACT))
    {
        // replace only if stored entry is not within aging factor of incoming entry
        if ((pEntry->h.nAge + pEntry->h.nDepth + HASH_AGING_FACTOR) >= (nHashAge + nDepth))
        {
#if LOG_HASH
            if (bLog)
                nHashBails++;
#endif
            return;
        }
    }
#endif	// if 0
#endif	// ALWAYS_REPLACE

Replace:
    if (abs(nEval) >= (CHECKMATE / 2))
    {
        if (nEval > 0)
            nEval += nPly;
        else
            nEval -= nPly;
    }

    HASH_ENTRY e;

#if USE_INTERLOCKED
    _InterlockedExchange64((volatile LONG64 *)&pEntry->h.dwSignature, dwSignature); // 8 byte atomic assignment of signature
#else
	pEntry->h.dwSignature = dwSignature;
#endif

    e.h.nDepth = (BYTE)nDepth;
    e.h.nEval = (short)nEval;
    e.h.nFlags = nFlags;
    if (cmMove)
    {
        e.h.moveflag = cmMove->moveflag;
        e.h.from = cmMove->fsquare;
        e.h.to = cmMove->tsquare;
    }
    else
        e.h.from = NO_SQUARE;

#if USE_INTERLOCKED
    _InterlockedExchange64((volatile LONG64 *)&pEntry->l[1] , e.l[1]); // 8 byte atomic assignment of all hash items except signature
#else
	pEntry->l[1] = e.l[1];
#endif
}

#if USE_PAWN_HASH
/*========================================================================
** ProbePawnHash - probes the pawn hash table for a matching entry
**========================================================================
*/
PAWN_HASH_ENTRY	*ProbePawnHash(PosSignature dwSignature)
{
    if (PawnHashTable == NULL)
        return(NULL);

    PosSignature	index = dwSignature & (dwPawnHashSize - 1);
    PAWN_HASH_ENTRY *pEntry = PawnHashTable + index;

#if 0 // LOG_HASH
    if (bLog)
        nHashProbes++;
#endif

    if (pEntry->dwSignature == dwSignature)
    {
        // verify that the position is identical
#if 0 // LOG_HASH
        if (bLog)
            nHashHits++;
#endif
        return(pEntry);
    }
    else
        return(NULL);
}

/*========================================================================
** SavePawnHash - saves a pawn structure and corresponding evaluation in
** the pawn hash table if applicable
**========================================================================
*/
void SavePawnHash(int mgEval, int egEval, PosSignature dwSignature)
{
    if (PawnHashTable == NULL)
        return;

#if 0 // LOG_HASH
    if (bLog)
        nHashSaves++;
#endif

    PosSignature	index = dwSignature & (dwPawnHashSize - 1);
    PAWN_HASH_ENTRY *pEntry = PawnHashTable + index;

    if (dwSignature == pEntry->dwSignature)
    {
#if 0 // LOG_HASH
        if (bLog)
            nHashBails++;
#endif
        return;
    }

#if USE_INTERLOCKED
    _InterlockedExchange64((volatile LONG64 *)&pEntry->nEval,  (long long)nEval);
    _InterlockedExchange64((volatile LONG64 *)&pEntry->dwSignature , dwSignature);
#else
	pEntry->mgEval = (short)mgEval;
    pEntry->egEval = (short)egEval;
    pEntry->dwSignature = dwSignature;
#endif
}
#endif	// USE_PAWN_HASH

#if USE_EVAL_HASH
/*========================================================================
** ProbeEvalHash - probes the eval hash table for a matching entry
**========================================================================
*/
EVAL_HASH_ENTRY *	ProbeEvalHash(PosSignature dwSignature)
{
    if (EvalHashTable == NULL)
        return(NULL);

#if 0 // LOG_HASH
    nEvalHashProbes++;
#endif

    PosSignature	index = dwSignature & (dwEvalHashSize - 1);
    EVAL_HASH_ENTRY *pEntry = EvalHashTable + index;

    if (pEntry->dwSignature == dwSignature)
    {
#if 0 // LOG_HASH
        nEvalHashHits++;
#endif
        return pEntry;
    }
    else
        return NULL;
}

/*========================================================================
** SavePawnHash - saves a position evaluation in the eval hash table
**========================================================================
*/
void SaveEvalHash(int nEval, PosSignature dwSignature)
{
    if (EvalHashTable == NULL)
        return;

    PosSignature	index = dwSignature & (dwEvalHashSize - 1);
    EVAL_HASH_ENTRY *pEntry = EvalHashTable + index;

    if (pEntry->dwSignature != dwSignature)
    {
#if 0 // LOG_HASH
        nEvalHashSaves++;
#endif
#if USE_INTERLOCKED
        _InterlockedExchange64((volatile LONG64 *)&pEntry->nEval,  (long long) nEval);
        _InterlockedExchange64((volatile LONG64 *)&pEntry->dwSignature , dwSignature);
#else
		pEntry->nEval = (short)nEval;
		pEntry->dwSignature = dwSignature;
#endif
    }
#if 0 // LOG_HASH
    else
        nEvalHashBails++;
#endif
}
#endif	// USE_EVAL_HASH

#if USE_HASH
/*========================================================================
** ClearHash - clears the hash table
**========================================================================
*/
void ClearHash(void)
{
#if USE_SMP
    if (bSlave)
        return;
#endif
	
	if (HashTable == NULL)
        return;

    memset(HashTable, 0, sizeof(HASH_ENTRY) * dwHashSize);

#if USE_EVAL_HASH
    if (EvalHashTable)
        memset(EvalHashTable, 0, sizeof(EVAL_HASH_ENTRY) * dwEvalHashSize);
#endif

#if USE_PAWN_HASH
    if (PawnHashTable)
        memset(PawnHashTable, 0, sizeof(PAWN_HASH_ENTRY) * dwPawnHashSize);
#endif
}

/*========================================================================
** InitHash - allocates the hash tables
**========================================================================
*/
HASH_ENTRY* InitHash(void)
{
    if (!bSlave)
    {
#if 0
        printf("# allocating hash table of %lld (%dMB) size\n", dwHashSize * sizeof(HASH_ENTRY),
               (dwHashSize * sizeof(HASH_ENTRY)) >> 20);
#endif

#if !USE_SMP
        if (bLog)
            fprintf(logfile, "allocating hash table of %ld (%dMB) size, each entry is %d bytes\n", dwHashSize * sizeof(HASH_ENTRY),
                    (dwHashSize * sizeof(HASH_ENTRY)) >> 20, sizeof(HASH_ENTRY));

        HashTable = (HASH_ENTRY *)malloc(dwHashSize * sizeof(HASH_ENTRY));

#if USE_EVAL_HASH
        if (bLog)
            fprintf(logfile, "allocating eval hash table of %ld (%dMB) size, each entry is %d bytes\n", dwEvalHashSize * sizeof(EVAL_HASH_ENTRY),
                    (dwEvalHashSize * sizeof(EVAL_HASH_ENTRY)) >> 20, sizeof(EVAL_HASH_ENTRY));

        EvalHashTable = (EVAL_HASH_ENTRY *)malloc(dwEvalHashSize * sizeof(EVAL_HASH_ENTRY));
#endif

#if USE_PAWN_HASH
        if (bLog)
            fprintf(logfile, "allocating pawn hash table of %ld (%dMB) size, each entry is %d bytes\n", dwPawnHashSize * sizeof(PAWN_HASH_ENTRY),
                    (dwPawnHashSize * sizeof(PAWN_HASH_ENTRY)) >> 20, sizeof(PAWN_HASH_ENTRY));

        PawnHashTable = (PAWN_HASH_ENTRY *)malloc(dwPawnHashSize * sizeof(PAWN_HASH_ENTRY));
#endif

        if (HashTable)
            ClearHash();
#endif	// !USE_SMP
    }

#if USE_SMP
    typedef  union  hilo {
        unsigned long a[2];
        unsigned long long b;
    } hilo;

    if (!bSlave)
    {
		// create shared memory for slave processes (even if they're aren't any, because of the "cores" command)
		time_t	t;
		hilo h={0};
   		h.b = dwHashSize * sizeof (HASH_ENTRY) + dwPawnHashSize * sizeof (PAWN_HASH_ENTRY) + dwEvalHashSize * sizeof (EVAL_HASH_ENTRY);

		sprintf(szSharedHashName, "MSH-%lld", (long long) time(&t));
		sprintf(szSharedMemName, "MSM-%lld", (long long) time(&t));
		hSharedHash = CreateFileMapping(
							INVALID_HANDLE_VALUE,    // use paging file
							NULL,                    // default security
							PAGE_READWRITE,          // read/write access
							h.a[1],   // maximum object size (high-order DWORD)
							h.a[0],  // maximum object size (low-order DWORD)
							szSharedHashName);       // name of mapping object

		if (hSharedHash != NULL)
		{
			HashTable = (HASH_ENTRY *)MapViewOfFile(hSharedHash,   // handle to map object
													FILE_MAP_ALL_ACCESS, // read/write permission
													0,
													0,
													0);

			PawnHashTable = (PAWN_HASH_ENTRY *) ((char *) HashTable + dwHashSize * sizeof (HASH_ENTRY));
			EvalHashTable = (EVAL_HASH_ENTRY *) ((char *) PawnHashTable +  dwPawnHashSize * sizeof (PAWN_HASH_ENTRY));
#if DEBUG_SMP
			printf("Master says hash = %08X, pawn = %08X, eval = %08X\n", &HashTable, &PawnHashTable, &EvalHashTable);
#endif
			if (HashTable)
				ClearHash();
		}

		hSharedMem = CreateFileMapping(
							INVALID_HANDLE_VALUE,    // use paging file
							NULL,                    // default security
							PAGE_READWRITE,          // read/write access
							0,                       // maximum object size (high-order DWORD)
							sizeof(SHARED_MEM),     // maximum object size (low-order DWORD)
							szSharedMemName);       // name of mapping object

		if (hSharedMem != NULL)
		{
			smSharedMem = (PSHARED_MEM) MapViewOfFile(hSharedMem,   // handle to map object
							FILE_MAP_ALL_ACCESS, // read/write permission
							0,
							0,
							sizeof(SHARED_MEM));

#if DEBUG_SMP
			if (smSharedMem != NULL)
				printf("Master says Shared Mem = %08X\n", &smSharedMem);
#endif
		}
    }
    else if (bSlave)
    {
        hSharedHash = OpenFileMapping(
                          FILE_MAP_ALL_ACCESS,   // read/write access
                          FALSE,                 // do not inherit the name
                          szSharedHashName);     // name of mapping object

        if (hSharedHash != NULL)
        {
            HashTable = (HASH_ENTRY *)MapViewOfFile(hSharedHash, // handle to map object
                                                    FILE_MAP_ALL_ACCESS,  // read/write permission
                                                    0,
                                                    0,
                                                    dwHashSize * sizeof (HASH_ENTRY) + dwPawnHashSize * sizeof (PAWN_HASH_ENTRY) + dwEvalHashSize * sizeof (EVAL_HASH_ENTRY));

            PawnHashTable = (PAWN_HASH_ENTRY *) ((char *) HashTable + dwHashSize * sizeof (HASH_ENTRY));
            EvalHashTable = (EVAL_HASH_ENTRY *) ((char *) PawnHashTable +  dwPawnHashSize * sizeof (PAWN_HASH_ENTRY));

#if DEBUG_SMP
            if (HashTable == NULL)
                printf("Could not map view of file (%s).\n", szSharedHashName);
            else
			{
                printf("Slave says hash = %08X, shared eval = %08X, shared pawn = %08X\n", &HashTable, &EvalHashTable, &PawnHashTable);
				printf("Slave says it is number %d\n", nSlaveNum);
			}
#endif
        }

        hSharedMem = OpenFileMapping(
                         FILE_MAP_ALL_ACCESS,   // read/write access
                         FALSE,                 // do not inherit the name
                         szSharedMemName);     // name of mapping object

        if (hSharedMem != NULL)
        {
            smSharedMem = (PSHARED_MEM)MapViewOfFile(hSharedMem, // handle to map object
                          FILE_MAP_ALL_ACCESS,  // read/write permission
                          0,
                          0,
                          sizeof(SHARED_MEM));

			ZeroMemory(&smSharedMem->sdSlaveData[nSlaveNum], sizeof(SLAVE_DATA));

#if DEBUG_SMP
            if (smSharedMem == NULL)
                printf("Could not map view of file (%s).\n", szSharedMemName);
            else
                printf("Slave says SharedMem = %08X\n", &smSharedMem);
#endif
        }
    }
#endif	// USE_SMP

    return(HashTable);
}

/*========================================================================
** CloseHash - frees the hash tables
**========================================================================
*/
void CloseHash(void)
{
    if (HashTable == NULL)
        return;

#if USE_SMP
    if (bSlave)
        return;
#endif

#if LOG_HASH
    if (bLog)
    {
        printf("Hash Stats: Saves = %d, Bails = %d, Probes = %d, Hits = %d, Returns = %d, Zeroes = %d\n",
                nHashSaves, nHashBails, nHashProbes, nHashHits, nHashReturns, nHashZeroes);
    }
#endif

#if USE_SMP
    if (hSharedHash)
        CloseHandle(hSharedHash);
#else
    free(HashTable);
#if USE_PAWN_HASH
    free(PawnHashTable);
#endif

#if USE_EVAL_HASH
    free(EvalHashTable);
#endif
#endif
}
#endif	// USE_HASH

/*========================================================================
** GetBBSignature -- get the Zobrist hash signature of a board position
**========================================================================
*/
PosSignature GetBBSignature(BB_BOARD *bbBoard)
{
    int				piece, color, sq;
    Bitboard		pieces;
    PosSignature	sig = 0;

    for (piece = KING; piece <= PAWN; piece++)
    {
        for (color = WHITE; color <= BLACK; color++)
        {
            pieces = bbBoard->bbPieces[piece][color];

            while (pieces)
            {
                sq = BitScan(PopLSB(&pieces));
                sig ^= aPArray[piece + (color * 6)][sq];
            }
        }
    }

    sig ^= aSTMArray[bbBoard->sidetomove];	// side to move
    sig ^= aCSArray[bbBoard->castles];	// castle status
    if (bbBoard->epSquare != NO_EN_PASSANT)
	{
		INDEX_CHECK(bbBoard->epSquare, aEPArray);
        sig ^= aEPArray[bbBoard->epSquare];
	}

    return(sig);
}

/*========================================================================
** GetBBPawnSignature -- get the Zobrist hash signature of a pawn structure
** and all other pieces related to the pawn hash (just Kings at this time)
**========================================================================
*/
PosSignature GetBBPawnSignature(BB_BOARD *bbBoard)
{
    int				color, sq;
    Bitboard		pieces;
    PosSignature	sig = 0;

    for (color = WHITE; color <= BLACK; color++)
    {
        pieces = bbBoard->bbPieces[PAWN][color];

        while (pieces)
        {
            sq = BitScan(PopLSB(&pieces));
            sig ^= aPArray[PAWN + (color * 6)][sq];
        }

        pieces = bbBoard->bbPieces[KING][color];

        while (pieces)
        {
            sq = BitScan(PopLSB(&pieces));
            sig ^= aPArray[KING + (color * 6)][sq];
        }
    }

    return(sig);
}
