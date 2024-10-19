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
along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

// Myrddin chess engine written by John Merlino, with lots of inspiration, assistance and patience (and perhaps some code) by:
// Ron Murawski - Horizon (great amounts of assistance as I was starting out, plus hosting Myrddin's site)
// Lars Hallerstrom - The Mad Tester!
// David Carteau - Orion and Cerebrum (provided Cerebrum NN library)
// Martin Sedlak - Cheng (guided me through Texel tuning)
// Dann Corbit - Helped bring the first bitboard version of Myrddin to release
// Bruce Moreland - Gerbil (well-documented code on his website taught me the basics)
// Tom Kerrigan - TSCP (a great starting point)
// Dr. Robert Hyatt - Crafty (helped us all)
// Ed Schröder and Jeroen Noomen - ProDeo Opening Book
// Andres Valverde - EveAnn and part of the Dirty team
// Pradu Kannan - Magicmoves bitboard move generator for sliding pieces
// Vladimir Medvedev - Greko (showed how strong a small, simple program could be)
// The Chessmaster Team - Lots of brilliant people, but mostly Johan de Koning (The King engine), Don Laabs, James Stoddard and Dave Cobb
//
// DONE -- add to release notes
// added NNUE probing code by David Carteau (Orion / Cerebrum)
// network created by me using games from CCRL, Lichess, and Myrddin testing (both self-play and against other engines)
// fixed a rare bug that could cause the best move from the previous iteration to not be the first move searched
// if perft or divide are called with no parameters, the default depth will be one (and Myrddin will no longer crash)
//
// TODO -- in no particular order

#include <conio.h>
#include <signal.h>
#include <direct.h>
#include <sys/timeb.h>
#include "myrddin.h"
#include "Bitboards.h"
#include "movegen.h"
#include "think.h"
#include "eval.h"
#include "fen.h"
#include "book.h"
#include "hash.h"
#include "tbprobe.h"
#include "gtb-probe.h"
#include "magicmoves.h"
#include "cerebrum.h"

char	*szVersion = "Myrddin 0.91";
char	*szInfo = "(10/20/24)";

CHESSMOVE	cmGameMoveList[MAX_MOVE_LIST];

// initialization file settings
BOOL 			bLog=FALSE;
BOOL			bKibitz=FALSE;
BOOL			bSlave=FALSE;
int				nCPUs = 1;
int				nEGTBCompressionType=tb_CP4;
char			szEGTBPath[MAX_PATH];
BOOL			bNoTB = FALSE;

// other (evil) globals
unsigned int	nGameMove;
int				nDepth, nThinkDepth, nPrevPVEval;
int				nCompSide;
unsigned int	nThinkTime, nPonderTime, nFischerInc, nLevelMoves, nMovesBeforeControl;
int				nClockRemaining;	// needs to be signed because it can be temporarily negative when calculating time to think
unsigned int	nCheckNodes;
CHESSMOVE		cmChosenMove, cmPonderMove;
BB_BOARD		bbPonderRestore;
clock_t			nThinkStart;
BOOL 			bPost, bStoreCommand, bInBook, bPondering, bXboard, bComputer, bExactThinkTime, bExactThinkDepth;
int				nEngineMode, nEngineCommand;
PosSignature	dwInitialPosSignature;
FILE		   *logfile;
char			line[512], command[512];
int				is_pipe = 0;
HANDLE			input_handle = 0;
int				nSlaveNum = -1;

extern NN_Accumulator accumulator;

#if USE_SMP
char			szProgName[MAX_PATH];
char			szSharedMemName[32], szSharedHashName[32];
HANDLE			hSharedMem = NULL;
HANDLE			hSharedHash = NULL;
SHARED_HASH		*shSharedHash;
SHARED_MEM		*smSharedMem;
STARTUPINFO		si;
PROCESS_INFORMATION	pi[MAX_CPUS];
#endif

const PieceType	BackRank[BSIZE] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};

#if USE_SMP
/*========================================================================
** SendSlavesString -- Sends (forwards) a command string to all slave 
** processes
**========================================================================
*/
void SendSlavesString(char *szString)
{
	int	x;
	char *c;

	if (nCPUs <= 1)
		return;

	if (!smSharedMem)
		return;

	// strip trailing carriage return if there is one
    c = strrchr(szString, '\n');
    if (c)
        *c = '\0';

#if DEBUG_SMP
	fprintf(logfile, "Sending slaves -- %s\n", szString);
#endif

	for (x = 0; x < nCPUs - 1; x++)
	{
		if (smSharedMem->sdSlaveData[x].bLocked || (strlen(smSharedMem->sdSlaveData[x].szEngineCommand[smSharedMem->sdSlaveData[x].nNextToSend])))
			Sleep(10);

		smSharedMem->sdSlaveData[x].bLocked = TRUE;
		strcpy(smSharedMem->sdSlaveData[x].szEngineCommand[smSharedMem->sdSlaveData[x].nNextToSend], szString);
		smSharedMem->sdSlaveData[x].nNextToSend = (smSharedMem->sdSlaveData[x].nNextToSend + 1) % NUM_SLAVE_STRINGS;
		smSharedMem->sdSlaveData[x].bLocked = FALSE;
	}
}

/*========================================================================
** ZeroSlaveNodes -- Clears the number of nodes searched by each process
**========================================================================
*/
void ZeroSlaveNodes(void)
{
	int x;

	if (nCPUs <= 1)
		return;

	if (!smSharedMem)
		return;

	for (x = 0; x < nCPUs - 1; x++)
		smSharedMem->sdSlaveData[x].nSearchNodes = 0;
}
#endif

/*========================================================================
** ParseIniFile -- Handle commands from the initialization file
**========================================================================
*/
void ParseIniFile(void)
{
    FILE   *IniFile;
    char	command[256];

    IniFile = fopen("Myrddin.ini", "rt");
    if (!IniFile)
    {
        printf("Myrddin.ini initialization file not found!\n");
        return;
    }

    while (fgets(command, 256, IniFile))
    {
        if (*command == 0)
            break;

        if (strnicmp(command, "logfile=", 8) == 0)
        {
            // "logfile" -- valid values: 1 = create logfile, 0 = do not create logfile
            bLog = (command[8] == '1');
        }
        else if (strnicmp(command, "kibitz=", 7) == 0)
        {
            // "kibitz" -- valid values: 1 = kibitz PV output, 0 = do not kibitz PV output
            bKibitz = (command[7] == '1');
        }
#if USE_HASH
        else if (strnicmp(command, "hashsize=", 9) == 0)
        {
            size_t	v;
            BOOL	bPowerOf2;

            // "hashsize" -- valid values: any number between 0 and 2048, specifying requested size in MB
            // preferably a power of 2, will round DOWN if not
            v = atoi(&command[9]);
            bPowerOf2 = !(v & (v - 1)) && v;	// thanks to http://graphics.stanford.edu/~seander/bithacks.html

            if (!bPowerOf2)
            {
                v >>= 1;		// round DOWN!
                v--;
                v |= v >> 1;
                v |= v >> 2;
                v |= v >> 4;
                v |= v >> 8;
                v |= v >> 16;
                v++;
            }

            dwHashSize = (v << 20);	// multiply by 1MB
            dwHashSize >>= 4;	// divide by size of hash entry (rounding up to power of 2)
        }
#endif
#if USE_EGTB
        else if (strnicmp(command, "egtbcompressiontype=", 20) == 0)
        {
            // "egtbcompressiontype" -- valid values: 0 = uncompressed, 1-4 = compression types 1-4 (4 is default and recommended)
            nEGTBCompressionType = atoi(&command[20]);
        }
        else if (strnicmp(command, "egtbfolder=", 10) == 0)
        {
            char *c;

            // "egtbfolder" -- valid values: valid folder location for EGTB files
            strcpy(szEGTBPath, command+11);

            // strip off trailing carriage return
            c = strrchr(szEGTBPath, '\n');
            if (c)
                *c = '\0';

            // strip off spaces
            c = strchr(szEGTBPath, ' ');
            if (c)
                *c = '\0';
        }
#endif
#if USE_SMP
        else if (strnicmp(command, "cpus=", 5) == 0)
		{
            // "cpus" -- number of cpus to use
            nCPUs = atoi(&command[5]);
			nCPUs = max(nCPUs, 1);
			nCPUs = min(nCPUs, MAX_CPUS);
#if DEBUG_SMP
			printf("Number of CPUS = %d\n", nCPUs);
#endif
		}
#endif
    }

    fclose(IniFile);
}

#if USE_SMP
/*========================================================================
** ParseCommandline -- Handle commands from the command line
**========================================================================
*/
void ParseCommandline(int argc, char *argv[])
{
    int		n = 1;
	char	command[64]={0};

    while (n < argc)
    {
        strcpy(command, argv[n]);

#if DEBUG_SMP
		printf("command %d is %s\n", n, command);
#endif

		if (strncmp(command, "sharedmem=", 10) == 0)
		{
			// "sharedmem" -- string containing name of shared memory for slave process game management
			if (bSlave)	// only do this if "slave" has been sent on the commandline first
				strcpy(szSharedMemName, command+10);
		}
		else if (strncmp(command, "sharedhash=", 11) == 0)
		{
			// "sharedhash" -- string containing name of shared memory for slave process transposition/eval/pawn hash 
			if (bSlave)	// only do this if "slave" has been sent on the commandline first
				strcpy(szSharedHashName, command+11);
		}
		else if (strncmp(command, "slave", 5) == 0)
		{
			// "slave" -- this is a slave process for SMP
			bSlave = TRUE;
		}
		else if (strncmp(command, "numslave=", 9) == 0)
		{
			// "numslave" -- this is the slave process number
			if (bSlave)	// only do this if "slave" has been sent on the commandline first
				nSlaveNum = atoi(&command[9]);
		}

        n++;
    }
}
#endif

/*========================================================================
** InitializeInput -- Open the stdin pipe for reading
**========================================================================
*/
void InitializeInput(void)
{
    DWORD	dw;

    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    is_pipe = !GetConsoleMode(input_handle, &dw);

    if (!is_pipe)
    {
        SetConsoleMode(input_handle, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
        FlushConsoleInputBuffer(input_handle);
    }

    setvbuf(stdin, (char*)NULL, _IONBF, 0);
    setvbuf(stdout, (char*)NULL, _IONBF, 0);

    signal(SIGINT, SIG_IGN);
}

/*========================================================================
** IsInputAvailable -- Check the stdin pipe for input
**========================================================================
*/
int IsInputAvailable(void)
{
    DWORD	nchars;

    if (is_pipe)
    {
        if (!PeekNamedPipe(input_handle, NULL, 0, NULL, &nchars, NULL))
            exit(-1);

        return (nchars);
    }
    else
        return(_kbhit());
}

/*========================================================================
** PromptForInput - Notify console window that input can now be given by
** the user
**========================================================================
*/
void PromptForInput(void)
{
    if (bSlave || bXboard)
        return;

    printf("> ");
    fflush(stdout);
}

/*========================================================================
** CheckForInput - Check for user input
**========================================================================
*/
BOOL	CheckForInput(BOOL bWaitForInput)
{
#if USE_SMP
	if (bSlave)
	{
		if (bWaitForInput)
		{
			while (strlen(smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]) == 0 || smSharedMem->sdSlaveData[nSlaveNum].bLocked)
				Sleep(10);
		}

		if (strlen(smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]) && !smSharedMem->sdSlaveData[nSlaveNum].bLocked)
		{
			smSharedMem->sdSlaveData[nSlaveNum].bLocked = TRUE;
#if DEBUG_SMP
			printf("slave %d has some input in slot %d -- '%s'\n", nSlaveNum, smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive, 
				smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]);
			fprintf(logfile, "slave %d has some input in slot %d -- '%s'\n", nSlaveNum, smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive,
				smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]);
#endif
			strcpy(line, smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]);
			char *c = strchr(smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive], ' ');
			if (c)
				*c = '\0';
			strcpy(command, smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive]);
			smSharedMem->sdSlaveData[nSlaveNum].szEngineCommand[smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive][0] = '\0';
			smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive = (smSharedMem->sdSlaveData[nSlaveNum].nNextToReceive + 1) % NUM_SLAVE_STRINGS;
			smSharedMem->sdSlaveData[nSlaveNum].bLocked = FALSE;
#if DEBUG_SMP
			printf("command = %s\n", command);
			printf("line = %s\n", line);
#endif
			return(TRUE);
		}
		else
			return(FALSE);
	}
#endif

    if (bWaitForInput == FALSE)
    {
        if (!IsInputAvailable())
            return(FALSE);
    }

    if (!fgets(line, 256, stdin))
        return(-1);	// shouldn't ever happen

    if (line[0] == '\n')
        return(FALSE);

    if (bLog)
        fprintf(logfile, "> Received %s", line);

    sscanf(line, "%s", command);

    return(TRUE);
}

/*========================================================================
** MoveToString -- converts a CHESSMOVE to a coordinate notation string
**========================================================================
*/
char *MoveToString(char *moveString, CHESSMOVE *move, BOOL bAddMove)
{
    sprintf(moveString, "%s%c%c%c%c", (bAddMove ? "move " : ""),
            BBSQ2COLNAME(move->fsquare), BBSQ2ROWNAME(move->fsquare),
            BBSQ2COLNAME(move->tsquare), BBSQ2ROWNAME(move->tsquare));

    if (move->moveflag & MOVE_PROMOTED)
    {
        int promotedPiece = move->moveflag & MOVE_PIECEMASK;

        switch (promotedPiece)
        {
			case QUEEN:
				strcat(moveString, "Q");
				break;
			case ROOK:
				strcat(moveString, "R");
				break;
			case BISHOP:
				strcat(moveString, "B");
				break;
			case KNIGHT:
				strcat(moveString, "N");
				break;
        }
    }

    return(moveString);
}

char* PVMoveToString(char* moveString, PVMOVE* move, BOOL bAddMove)
{
	sprintf(moveString, "%s%c%c%c%c", (bAddMove ? "move " : ""),
		BBSQ2COLNAME(move->fsquare), BBSQ2ROWNAME(move->fsquare),
		BBSQ2COLNAME(move->tsquare), BBSQ2ROWNAME(move->tsquare));

	if (move->moveflag & MOVE_PROMOTED)
	{
		int promotedPiece = move->moveflag & MOVE_PIECEMASK;

		switch (promotedPiece)
		{
		case QUEEN:
			strcat(moveString, "Q");
			break;
		case ROOK:
			strcat(moveString, "R");
			break;
		case BISHOP:
			strcat(moveString, "B");
			break;
		case KNIGHT:
			strcat(moveString, "N");
			break;
		}
	}

	return(moveString);
}

/*========================================================================
** bbNewGame -- Resets the game board to a new game in the initial position
**========================================================================
*/
void bbNewGame(BB_BOARD *bbBoard)
{
    int x;

#if USE_HASH
//	nHashAge = 0;
    ClearHash();
#endif

#if USE_HISTORY
    ClearHistory();
#endif

#if USE_KILLERS
    ClearKillers(FALSE);
#endif

    ZeroMemory(bbBoard, sizeof(BB_BOARD));

    bbBoard->bbPieces[KING][WHITE] = Bit[BB_E1];
    bbBoard->bbPieces[KING][BLACK] = Bit[BB_E8];

    bbBoard->bbPieces[QUEEN][WHITE] = Bit[BB_D1];
    bbBoard->bbPieces[QUEEN][BLACK] = Bit[BB_D8];

    bbBoard->bbPieces[ROOK][WHITE] = Bit[BB_A1] | Bit[BB_H1];
    bbBoard->bbPieces[ROOK][BLACK] = Bit[BB_A8] | Bit[BB_H8];

    bbBoard->bbPieces[BISHOP][WHITE] = Bit[BB_C1] | Bit[BB_F1];
    bbBoard->bbPieces[BISHOP][BLACK] = Bit[BB_C8] | Bit[BB_F8];

    bbBoard->bbPieces[KNIGHT][WHITE] = Bit[BB_B1] | Bit[BB_G1];
    bbBoard->bbPieces[KNIGHT][BLACK] = Bit[BB_B8] | Bit[BB_G8];

    bbBoard->bbPieces[PAWN][WHITE] = BB_RANK_2;
    bbBoard->bbPieces[PAWN][BLACK] = BB_RANK_7;

    bbBoard->bbMaterial[WHITE] = BB_RANK_1 | BB_RANK_2;
    bbBoard->bbMaterial[BLACK] = BB_RANK_7 | BB_RANK_8;

    bbBoard->bbOccupancy = BB_RANK_1 | BB_RANK_2 | BB_RANK_7 | BB_RANK_8;

    ZeroMemory(&bbBoard->squares, sizeof(bbBoard->squares));
    for (x = 0; x < 8; x++)
    {
        bbBoard->squares[x] = XBLACK|BackRank[x];
        bbBoard->squares[x + 8] = BLACK_PAWN;
        bbBoard->squares[x + 48] = WHITE_PAWN;
        bbBoard->squares[x + 56] = XWHITE|BackRank[x];
    }

    bbBoard->sidetomove = WHITE;

    bbBoard->castles = WHITE_KINGSIDE_BIT | WHITE_QUEENSIDE_BIT | BLACK_KINGSIDE_BIT | BLACK_QUEENSIDE_BIT;
    bbBoard->epSquare = NO_SQUARE;
    bbBoard->fifty = 0;
    bbBoard->inCheck = FALSE;
    bbBoard->signature = GetBBSignature(bbBoard);

	nn_update_all_pieces(accumulator, bbBoard->bbPieces);

    ZeroMemory(cmGameMoveList, sizeof(cmGameMoveList));
    nGameMove = 0;

    bInBook = TRUE;
    bComputer = FALSE;

    if (bKibitz)
        printf("tellics kibitz Hello, this is Myrddin, a fledgling chess engine that plays around 2600 ELO. Thanks for playing!\n");
}

/*========================================================================
** GamePositionRepeated - Checks to see if the position on the game board
** has ever occurred in the game
**========================================================================
*/
BOOL GamePositionRepeated(PosSignature dwSignature)
{
    int	n, nReps;

    nReps = 0;

    for (n = nGameMove-2; n >= 0; n--)
    {
        if (cmGameMoveList[n].dwSignature == dwSignature)
        {
            nReps++;
            if (nReps >= 2)
                return(TRUE);
        }

        if (cmGameMoveList[n].dwSignature == 0)	// bail if we find a null move, is this correct?
            return(FALSE);
    }

    return(FALSE);
}

/*========================================================================
** BoardIsMaterialDraw - Checks to see if the material on the game board
** is insufficient for either side to checkmate
**========================================================================
*/
BOOL BoardIsMaterialDraw(BB_BOARD *board)
{
    if (board->bbPieces[QUEEN][WHITE] || board->bbPieces[QUEEN][BLACK]
            || board->bbPieces[ROOK][WHITE] || board->bbPieces[ROOK][BLACK]
            || board->bbPieces[PAWN][WHITE] || board->bbPieces[PAWN][BLACK])
        return(FALSE);

    if (BitCount(board->bbPieces[BISHOP][WHITE] | board->bbPieces[BISHOP][BLACK]
                 | board->bbPieces[KNIGHT][WHITE] | board->bbPieces[KNIGHT][BLACK]) == 1)
        return(TRUE);	// either kings only or just one minor left

    return(FALSE);
}

/*========================================================================
** NotHandled - generic error message when a command can't be handled
** because the engine is thinking
**========================================================================
*/
void	NotHandled(void)
{
    if (bLog)
        fprintf(logfile, "%s command not handled while engine is %s\n", command,
                (nEngineMode == ENGINE_ANALYZING ? "analyzing" : "thinking"));

    printf("%s command not handled while engine is %s\n", command,
           (nEngineMode == ENGINE_ANALYZING ? "analyzing" : "thinking"));
}

/*========================================================================
** HandleCommand - parse and handle input commands
**========================================================================
*/
void	HandleCommand(void)
{
	if (strlen(command) == 0)
		return;

    if (bLog)
        fprintf(logfile, "Handling Command %s\n", command);

    if (!strcmp(command, "protover"))
    {
        int	version;

        sscanf(line, "%s %d", command, &version);
        if (version == 2)
        {
            printf("feature done=0\n");
            printf("feature setboard=1 playother=1 draw=0\n");
            printf("feature sigint=0 sigterm=0 reuse=0 analyze=1\n");
			printf("feature variants=normal\n");
			printf("feature myname=\"%s\"\n", szVersion);
            printf("feature done=1\n");
            fflush(stdout);
        }

        if (bLog)
            fprintf(logfile, "< Finished protover\n");

        PromptForInput();

        return;
    }

    if (!strcmp(command, "xboard"))
    {
        bXboard = TRUE;
        return;
    }

    if (!strcmp(command, "new"))
    {
#if USE_SMP
		if (!bSlave)
			SendSlavesString(command);
#endif

        if ((bStoreCommand == FALSE) &&
                ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_ANALYZING) || (nEngineMode == ENGINE_PONDERING)))
        {
            nEngineCommand = STOP_THINKING;
            bStoreCommand = TRUE;
            return;
        }

        bbNewGame(&bbBoard);

        nFischerInc = 0;	// reset the fischer increment

        nCompSide = BLACK;
        if (nEngineMode != ENGINE_ANALYZING)
            PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished new\n");

        bStoreCommand = FALSE;
        return;
    }

    if (!strcmp(command, "setboard") || !strcmp(command, "loadfen"))
    {
        char   *c;

#if USE_SMP
		if (!bSlave)
			SendSlavesString(line);
#endif

        if ((bStoreCommand == FALSE) &&
                ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_ANALYZING) || (nEngineMode == ENGINE_PONDERING)))
        {
            nEngineCommand = STOP_THINKING;
            bStoreCommand = TRUE;
            return;
        }

        if (strlen(line) <= 10)
        {
            printf("Invalid command: %s\n", command);
            PromptForInput();
            return;
        }

        // strip trailing carriage return if there is one
        c = strrchr(line, '\n');
        if (c)
            *c = '\0';

        bbNewGame(&bbBoard);
        if (BBForsytheToBoard(line + 9, &bbBoard) == -1)
		{
			printf("Error parsing FEN %s\nstarting new game\n", line + 9);
			bbNewGame(&bbBoard);
			PromptForInput();
			return;
		}
        bbBoard.inCheck = BBKingInDanger(&bbBoard, bbBoard.sidetomove);
        dwInitialPosSignature = bbBoard.signature = GetBBSignature(&bbBoard);

		nn_update_all_pieces(accumulator, bbBoard.bbPieces);

#if 0
        WORD	x = 0;
        char	buf[128];
        CHESSMOVE	cmEvalMoveList[MAX_LEGAL_MOVES];

        BBGenerateAllMoves(&bbBoard, &cmEvalMoveList[0], &x, FALSE);
        printf("%d moves from position %s\n", x, BBBoardToForsythe(&bbBoard, 0, buf));
#endif

        nCompSide = NO_SIDE;
        if (nEngineMode != ENGINE_ANALYZING)
			PromptForInput();

        if (bLog)
			fprintf(logfile, "< Finished setboard\n");

        bStoreCommand = FALSE;
        return;
    }

#if USE_SMP
    if (!strncmp(command, "cores", 5))
	{
		if (bSlave)
			return;

        if (nEngineMode == ENGINE_THINKING || nEngineMode == ENGINE_ANALYZING || nEngineMode == ENGINE_PONDERING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

		KillProcesses();

		if (command[5] == '=')
			nCPUs = atoi(&command[6]);
		else
			sscanf(line, "%s %d", command, &nCPUs);
		if (nCPUs < 1)
			nCPUs = 1;
		if (nCPUs > MAX_CPUS)
			nCPUs = MAX_CPUS;

		StartProcesses(nCPUs);

        if (bLog)
            fprintf(logfile, "Now using %d processes\n", nCPUs);

        printf("Now using %d processes\n\n", nCPUs);

        PromptForInput();
        return;
	}
#endif

    if (!strcmp(command, "perft"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif

        int	depth = 0;
        clock_t	starttime;

        if (nEngineMode == ENGINE_THINKING || nEngineMode == ENGINE_ANALYZING || nEngineMode == ENGINE_PONDERING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        sscanf(line, "%s %d", command, &depth);

		if (depth <= 0)
			depth = 1;

        starttime = GetTickCount();
        nPerftMoves = doBBPerft(depth, &bbBoard, FALSE);
#if USE_BULK_COUNTING
        printf("Using bulk counting... ");
#endif

        printf("perft %d = %I64u in %.2f seconds\n", depth, nPerftMoves, (float)((GetTickCount() - starttime)) / 1000);

#if !USE_BULK_COUNTING
        printf("%ld KNPS\n", nPerftMoves / (GetTickCount() - starttime));
#endif

        PromptForInput();
        return;
    }

    if (!strcmp(command, "divide"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif

        int	depth;
        clock_t	starttime;

        if (nEngineMode == ENGINE_THINKING || nEngineMode == ENGINE_ANALYZING || nEngineMode == ENGINE_PONDERING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        sscanf(line, "%s %d", command, &depth);

		if (depth <= 0)
			depth = 1;

#if USE_BULK_COUNTING
		printf("Using bulk counting...\n");
#endif
		starttime = GetTickCount();
        nPerftMoves = doBBPerft(depth, &bbBoard, TRUE);
        printf("perft %d = %I64u in time %.2f\n", depth, nPerftMoves, (float)((GetTickCount() - starttime)) / 1000);

        PromptForInput();
        return;
    }

	if (!strcmp(command, "rpt"))	// run perft test
	{
#if USE_SMP
		if (bSlave)
			return;
#endif
		int x;
		clock_t alltime, starttime;

#if USE_BULK_COUNTING
		printf("Using bulk counting...\n");
#endif
		alltime = GetTickCount();
		for (x = 0; x < NUM_PERFT_TESTS; x++)
		{
			bbNewGame(&bbBoard);
			BBForsytheToBoard(perft_tests[x].fen, &bbBoard);
			bbBoard.inCheck = BBKingInDanger(&bbBoard, bbBoard.sidetomove);
			dwInitialPosSignature = bbBoard.signature = GetBBSignature(&bbBoard);

			printf("%d) %s - ", x+1, perft_tests[x].fen);
			starttime = GetTickCount();
			nPerftMoves = doBBPerft(perft_tests[x].depth, &bbBoard, FALSE);

			printf("perft %d = %I64u in %.2f seconds - ", perft_tests[x].depth, nPerftMoves, (float)((GetTickCount() - starttime)) / 1000);
			if (nPerftMoves != perft_tests[x].value)
				printf("FAILED! Should be %I64u\n", perft_tests[x].value);
			else
				printf("passed\n");
		}

		printf("Total Time = %.2f seconds\n", (float)((GetTickCount() - alltime)) / 1000);
		PromptForInput();
		return;
	}

    if (!strcmp(command, "eval"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif

        if (nEngineMode != ENGINE_IDLE)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        int	nResult;

#if USE_EGTB
        if (tb_available && (BitCount(bbBoard.bbOccupancy) <= 5))
			nResult = GaviotaTBProbe(&bbBoard, FALSE);
        else
#endif
			nResult = BBEvaluate(&bbBoard, -CHECKMATE-1, CHECKMATE+1);

        printf("score = %d\n", nResult);

        if (bbBoard.sidetomove == BLACK)
			nResult *= -1;

        printf("static evaluation of %s = %d, sig = %016I64X\n", BBBoardToForsythe(&bbBoard, 0, line), nResult, GetBBSignature(&bbBoard));

        PromptForInput();

        if (bLog)
        fprintf(logfile, "< Finished eval\n");

        return;
    }

    if (!strcmp(command, "see"))
    {
        WORD		nNumMoves;
        CHESSMOVE	cmSEEMoveList[MAX_LEGAL_MOVES];
        char        from[3], to[3];
        int         x, fsquare, tsquare;

        BBGenerateAllMoves(&bbBoard, cmSEEMoveList, &nNumMoves, FALSE);

        sscanf(line, "%s %s %s", command, from, to);
        fsquare = SqNameToSq(from);
        tsquare = SqNameToSq(to);

        for (x = 0; x < nNumMoves; x++)
        {
            if ((cmSEEMoveList[x].fsquare == fsquare) && (cmSEEMoveList[x].tsquare == tsquare))
                break;
        }
        if (x == nNumMoves)
            printf("Move Not Found! %d moves, from=%d, to=%d\n", nNumMoves, fsquare, tsquare);
        else
        {
            bbEvalBoard = bbBoard;
            printf("SEE Val of %s%s = %d\n", from, to, BBSEEMove(&cmSEEMoveList[x], bbBoard.sidetomove));
        }

        PromptForInput();

        return;
    }

	if (!strcmp(command, "tb"))
	{
#if USE_SMP
		if (!bSlave)
			SendSlavesString(command);
#endif

		if (tb_available)
		{
			printf("Tablebase support is now OFF\n");
			tb_available = FALSE;
		}
		else if (!bNoTB)
		{
			printf("Tablebase support is now ON\n");
			tb_available = TRUE;
		}
		else
			printf("Gaviota Tablebases not available!\n");

		PromptForInput();

		return;
	}

	if (!strcmp(command, "quit"))
    {
#if USE_SMP
		if (!bSlave)
			SendSlavesString(command);
#endif
#if USE_HASH
        CloseHash();
#endif
        if (bLog)
        {
            fflush(logfile);
            fclose(logfile);
        }

#if USE_EGTB
        GaviotaTBClose();
#endif
        exit(0);
    }

    if (!strcmp(command, "exit"))
    {
        if (nEngineMode == ENGINE_ANALYZING)
		{
#if USE_SMP
			if (!bSlave)
				SendSlavesString(command);
#endif
			nEngineMode = ENGINE_IDLE;
			nEngineCommand = STOP_THINKING;
			nCompSide = NO_SIDE;

            if (bLog)
                fflush(logfile);

			return;
		}
		else
		{
#if USE_SMP
			if (!bSlave)
				SendSlavesString("quit");
#endif
#if USE_HASH
	        CloseHash();
#endif
            if (bLog)
            {
                fflush(logfile);
                fclose(logfile);
            }
#if USE_EGTB
			GaviotaTBClose();
#endif
			exit(0);
		}
    }

    if (!strcmp(command, "go"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif

        if (nEngineMode != ENGINE_IDLE)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        nCompSide = bbBoard.sidetomove;

        if (bLog)
	        fprintf(logfile, "< Finished go\n");
        return;
    }

#if USE_SMP
	if (!strcmp(command, "stop"))	// special command to tell slaves to stop thinking
	{
		if (bSlave && (nEngineMode == ENGINE_ANALYZING))
		{
			nEngineMode = ENGINE_IDLE;
			nEngineCommand = STOP_THINKING;
			nCompSide = NO_SIDE;
		}

		return;
	}
#endif

    if (!strcmp(command, "force"))
    {
#if USE_SMP
		if (!bSlave)
			SendSlavesString("stop");
#endif

        if (nEngineMode == ENGINE_ANALYZING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        if ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_PONDERING))
			nEngineCommand = STOP_THINKING;

		if (nEngineMode == ENGINE_PONDERING)	// have to back out the pondering move before setting the engine idle
		{
			bbBoard = bbPonderRestore;
			ZeroMemory(&cmGameMoveList[--nGameMove], sizeof(CHESSMOVE));
		}

        nCompSide = NO_SIDE;
        PromptForInput();

        if (bLog)
	        fprintf(logfile, "< Finished force\n");
        return;
    }

    if (!strcmp(command, "white"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        if ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_PONDERING))
			nEngineCommand = STOP_THINKING;

        bbBoard.sidetomove = WHITE;
        nCompSide = BLACK;
        PromptForInput();

        if (bLog)
	        fprintf(logfile, "< Finished white\n");
        return;
    }

    if (!strcmp(command, "black"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        if ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_PONDERING))
	        nEngineCommand = STOP_THINKING;

        bbBoard.sidetomove = BLACK;
        nCompSide = WHITE;
        PromptForInput();

        if (bLog)
			fprintf(logfile, "< Finished black\n");
        return;
    }

    if (!strcmp(command, "playother"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
		{
			NotHandled();
			PromptForInput();
			return;
		}

        if ((nEngineMode == ENGINE_THINKING) || (nEngineCommand == ENGINE_PONDERING))
	        nEngineCommand = STOP_THINKING;

        nCompSide = OPPONENT(bbBoard.sidetomove);
        PromptForInput();

        if (bLog)
		    fprintf(logfile, "< Finished playother\n");
        return;
    }

    if (!strcmp(command, "?"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if ((nEngineMode == ENGINE_ANALYZING) || (nEngineMode == ENGINE_PONDERING))
		{
			NotHandled();
			PromptForInput();
			return;
		}

        if (nEngineMode == ENGINE_THINKING)
	        nEngineCommand = END_THINKING;

        PromptForInput();

        if (bLog)
			fprintf(logfile, "< Finished ?\n");
        return;
    }

    if (!strcmp(command, "st"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        sscanf(line, "st %d", &nThinkTime);
        bExactThinkTime = TRUE;
        bExactThinkDepth = FALSE;
        nCheckNodes = 0xFFFF;

        if (bLog)
	        fprintf(logfile, "< Finished st\n");
        PromptForInput();
        return;
    }

    if (!strcmp(command, "sd"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        sscanf(line, "sd %d", &nThinkDepth);
        bExactThinkDepth = TRUE;
        bExactThinkTime = FALSE;
        nCheckNodes = 0xFFFF;

        if (bLog)
	        fprintf(logfile, "< Finished sd\n");
        PromptForInput();
        return;
    }

    if (!strcmp(command, "level"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        char	szTime[16];

        // Myrddin doesn't have an internal clock, and gets all of its clock info from the "time" command. However,
        // for Fischer style controls, we can add a extra time per move (I've chosen 90% of the increment, just to
        // save a little buffer)
        sscanf(line, "%s %d %s %d", command, &nLevelMoves, szTime, &nFischerInc);

        nFischerInc *= 900;	// convert to milliseconds and take most of it, just to be safe
        bExactThinkTime = FALSE;
        bExactThinkDepth = FALSE;

        if (nEngineMode != ENGINE_THINKING)
            PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished level, nFischerInc = %d, nLevelMoves = %d\n", nFischerInc, nLevelMoves);
        return;
    }

    if (!strcmp(command, "time"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        int	nTime, nDivisor;

        // GetTickCount() returns 1000's of a second, but Winboard uses 100's of a second
        // so multiply what Winboard reports by 10 to match that scale. Then divide
        // by 30 so that, by default, the engine thinks for no more than 1/30 of the
        // available time.
        sscanf(line, "time %d", &nTime);
        nDivisor = CLOCK_TO_USE;

        if (nTime <= 0)
        {
            if (bLog)
                fprintf(logfile, "Clock is negative = %d!\n", nTime);

            nClockRemaining = 0;	// somehow the game is still going even though we're out of time,
            // so use as little time as possible
        }
        else
            nClockRemaining = nTime * 10;

        nClockRemaining -= TIME_BANK;	// use a "bank" of two seconds, primarily for bullet time controls
        if (nClockRemaining <= 0)
            nClockRemaining = 0;

        if (nClockRemaining <= PANIC_THRESHHOLD)
            nDivisor = PANIC_CLOCK_TO_USE;

        if (nFischerInc > 0)	// Fischer controls
            nThinkTime = (nClockRemaining / nDivisor) + nFischerInc;
        else if (nLevelMoves > 0)	// Moves/Minutes
        {
            nMovesBeforeControl = nLevelMoves - (((nGameMove + 1) / 2) % nLevelMoves);

            nThinkTime = nClockRemaining / nMovesBeforeControl;
        }
        else	// Game in N Minutes
            nThinkTime = (nClockRemaining / nDivisor);

        // check
        if (nThinkTime / 1000 >= 60) // ten seconds or more to think, check every 64K nodes (about 20x per second)
            nCheckNodes = 0xFFFF;
        else if (nThinkTime / 1000 >= 2) // two seconds = check every 32K nodes
            nCheckNodes = 0x7FFF;
        else
            nCheckNodes = 0x3FFF;	// less than two seconds = check every 16K nodes

        PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished time -- nThinkTime = %d, nGameMove = %d\n", nThinkTime, nGameMove);
        return;
    }

    if (!strcmp(command, "undo"))
    {
#if USE_SMP
		if (!bSlave)
			SendSlavesString(command);
#endif
        if (nGameMove == 0)
        {
            printf("No moves to undo!\n");
            if (nEngineMode != ENGINE_ANALYZING)
                PromptForInput();
            return;
        }

        if ((bStoreCommand == FALSE) &&
                ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_ANALYZING) || (nEngineMode == ENGINE_PONDERING)))
        {
            nEngineCommand = END_THINKING;
            bStoreCommand = TRUE;
            return;
        }

        BBUnMakeMove(&cmGameMoveList[nGameMove-1], &bbBoard);

        // undo is permanent!
        ZeroMemory(&cmGameMoveList[--nGameMove], sizeof(CHESSMOVE));

        nCompSide = NO_SIDE;
        if (nEngineMode != ENGINE_ANALYZING)
            PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished undo\n");

        bStoreCommand = FALSE;
        return;
    }

    if (!strcmp(command, "post"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
        {
            NotHandled();
            PromptForInput();
            return;
        }

        bPost = TRUE;
        if ((nEngineMode != ENGINE_THINKING) && (nEngineMode != ENGINE_PONDERING))
            PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished post\n");
        return;
    }

    if (!strcmp(command, "nopost"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
        {
            NotHandled();
            PromptForInput();
            return;
        }

        bPost = FALSE;
        if ((nEngineMode != ENGINE_THINKING) && (nEngineMode != ENGINE_PONDERING))
            PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished nopost\n");
        return;
    }

    if (!strcmp(command, "result"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        if (nEngineMode == ENGINE_ANALYZING)
        {
            NotHandled();
            return;
        }

        if ((nEngineMode == ENGINE_THINKING) || (nEngineMode == ENGINE_PONDERING))
            nEngineCommand = STOP_THINKING;
        nCompSide = NO_SIDE;

        PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished result\n");

        if (bKibitz)
            printf("tellics kibitz Good Game!\n");

        return;
    }

    if (!strcmp(command, "analyze"))
    {
        nEngineMode = ENGINE_ANALYZING;
        nThinkTime = 0xFFFFFFFF;
		if (bSlave)
			nCheckNodes = 0x3FFF;	// slaves need to check often for input, so every 32K nodes (about 50x per second)
		else
			nCheckNodes = 0x1FFFF;	// every 128K nodes, about 10x second, should be enough
#if USE_HASH
        ClearHash();
#endif

        if (bLog)
            fprintf(logfile, "< Finished analyze\n");
        return;
    }

    if (!strcmp(command, "."))
    {
        if (nEngineMode == ENGINE_ANALYZING)
            printf("update command not implemented\n");

        if (bLog)
            fprintf(logfile, "< Finished .\n");
        return;
    }

    if (!strcmp(command, "hard"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        bPondering = TRUE;
        PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished hard\n");
        return;
    }

    if (!strcmp(command, "easy"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        bPondering = FALSE;
        PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished easy\n");
        return;
    }

    if (!strcmp(command, "computer"))
    {
#if USE_SMP
		if (bSlave)
			return;
#endif
        bComputer = TRUE;
        PromptForInput();

        if (bLog)
            fprintf(logfile, "< Finished computer\n");
        return;
    }

    if (!strcmp(command, "otim") ||
        !strcmp(command, "ping") ||
        !strcmp(command, "random") ||
        !strcmp(command, "accepted") ||
        !strcmp(command, "rejected") ||
		!strcmp(command, "variant") ||
		!strcmp(command, "usermove") ||
        !strcmp(command, "name") ||
        !strcmp(command, "ics") ||
        !strcmp(command, "pause") ||
        !strcmp(command, "resume") ||
        !strcmp(command, "rating") ||
        !strcmp(command, "draw") ||
        !strcmp(command, "remove") ||
        !strcmp(command, "hint") ||
        !strcmp(command, ".") ||
        !strcmp(command, "edit"))
    {
        if (bLog)
            fprintf(logfile, "< %s not supported\n", command);

        if (nEngineMode != ENGINE_THINKING)
            PromptForInput();

        return;
    }

    // we probably have a move from the opponent, or a move from the GUI in force mode
    {
        CHESSMOVE	cmTempMoveList[MAX_LEGAL_MOVES];
        char		moveString[8];
        BOOL		bMoveMade = FALSE;
        WORD		nNumMoves;
        int			n;

        if (bLog)
            fprintf(logfile, "In move handling with %s\n", command);

        if (nEngineMode == ENGINE_THINKING)
        {
            NotHandled();
            return;
        }

        if ((nEngineMode == ENGINE_ANALYZING) && (bStoreCommand == FALSE))
        {
            // we need to stop the engine analyzing before making the move, or things will get ugly
            nEngineCommand = STOP_THINKING;
            bStoreCommand = TRUE;
            nCompSide = NO_SIDE;
            return;
        }

        if (bLog)
        {
            fprintf(logfile, "We just got a move -- nSideToMove = %d, nEngineMode == %d, nEngineCommand = %d\n",
                    bbBoard.sidetomove, nEngineMode, nEngineCommand);
        }

        if (nEngineMode == ENGINE_PONDERING)
        {
            if (bLog)
                fprintf(logfile, "Backing out the pondering move\n");

            // back out the pondering move
			bbBoard = bbPonderRestore;
            ZeroMemory(&cmGameMoveList[--nGameMove], sizeof(CHESSMOVE));
        }

        BBGenerateAllMoves(&bbBoard, cmTempMoveList, &nNumMoves, FALSE);

        // compare the received move against all legal moves
		for (n = 0; n < nNumMoves; n++)
        {
            MoveToString(moveString, &cmTempMoveList[n], FALSE);

			if (!strnicmp(moveString, command, strlen(moveString)))	// we've found our move
            {
#if USE_SMP
				if (!bSlave)
					SendSlavesString(moveString);
#endif

                // make the move on the board and update the official game movelist
                BBMakeMove(&cmTempMoveList[n], &bbBoard);

                if (bLog)
                    fprintf(logfile, "< move accepted: %s, nFifty=%d\n", moveString, bbBoard.fifty);

                cmTempMoveList[n].dwSignature = bbBoard.signature;
                cmGameMoveList[nGameMove++] = cmTempMoveList[n];

                // check for draw
                if (GamePositionRepeated(cmTempMoveList[n].dwSignature))
                {
                    if (bLog)
                        fprintf(logfile, "1/2-1/2 {Draw by Repetition}\n");

                    printf("1/2-1/2 {Draw by Repetition}\n");
                }
                else if (bbBoard.fifty >= 100)
                {
                    if (bLog)
                        fprintf(logfile, "1/2-1/2 {Draw by 50-move rule}\n");

                    printf("1/2-1/2 {Draw by 50-move rule}\n");
                }
                else if (BoardIsMaterialDraw(&bbBoard))
                {
                    if (bLog)
                        fprintf(logfile, "1/2-1/2 {Draw by Insufficient Material}\n");

                    printf("1/2-1/2 {Draw by Insufficient Material}\n");
                }

                bMoveMade = TRUE;
                break;
            }
        }

        if (bMoveMade == FALSE)
        {
            printf("< Illegal move/command:%s\n", command);

            if (bLog)
			{
				char	buf[128];

                fprintf(logfile, "< Illegal move/command:%s\n", command);
				fprintf(logfile, "    FEN is %s\n", BBBoardToForsythe(&bbBoard, 0, buf));
			}
        }
        else if (nEngineMode == ENGINE_PONDERING)
        {
            if ((cmTempMoveList[n].fsquare == cmPonderMove.fsquare) &&
                    (cmTempMoveList[n].tsquare == cmPonderMove.tsquare) &&
                    (cmTempMoveList[n].moveflag == cmPonderMove.moveflag))
            {
                // we got a hit on the ponder move
                if (bLog)
                    fprintf(logfile, "Ponder hit!\n");

                nPonderTime = GetTickCount() - nThinkStart;

                nEngineMode = ENGINE_THINKING;

                // now check to see if there is a book move
                BBBoardToForsythe(&bbBoard, 0, EPD);
                FIND_OPENING();
                if (*FROM)
                {
                    nEngineCommand = STOP_THINKING;
                    bInBook = TRUE;
                }
            }
            else
            {
                // no hit on the ponder move, start thinking as normal
                if (bLog)
                    fprintf(logfile, "No Ponder hit!\n");

                nEngineCommand = END_THINKING;
            }
        }

        if ((nEngineMode != ENGINE_ANALYZING) && (nEngineMode != ENGINE_PONDERING))
            PromptForInput();

        bStoreCommand = FALSE;
    }

    return;
}

/*========================================================================
** PrintPV -- Display a PV
**========================================================================
*/
void PrintPV(int nPVEval, int nSideToMove, char comment, BOOL bPrintKibitz)
{
    int			n;
    char		moveString[24];
	char		buf[1024]={0};
	unsigned long long	nNodes = nSearchNodes;

    if ((nEngineMode == ENGINE_ANALYZING) && (nSideToMove == BLACK))
        nPVEval = -nPVEval;

#if USE_SMP
	if (!bSlave)
	{
		for (n = 0; n < nCPUs - 1; n++)
			nNodes += smSharedMem->sdSlaveData[n].nSearchNodes;
	}
#endif

    sprintf(buf, "%2d %6d %6d %12llu ", nDepth, nPVEval, (GetTickCount()-nThinkStart) / 10, nNodes);

    if (nEngineMode == ENGINE_PONDERING)
    {
        strcat(buf, "(");
        MoveToString(moveString, &cmPonderMove, FALSE);
        strcat(buf, moveString);
        strcat(buf, ") ");
    }

    for (n = 0; n < evalPV.pvLength; n++)
    {
        if (nPVEval + n >= CHECKMATE)
            break;

        PVMoveToString(moveString, &evalPV.pv[n], FALSE);
        strcat(buf, moveString);
		if ((n == 0) && comment)
		{
			moveString[0] = comment;
			moveString[1] = '\0';
			strcat(buf, moveString);
		}
        strcat(buf, " ");
		if (comment)
			break;
	}

    if ((GetTickCount() - nThinkStart) > 0)
    {
        sprintf(moveString, "(%lld KNPS)", nNodes / (GetTickCount() - nThinkStart));
        strcat(buf, moveString);
    }

	if ((abs(nPVEval) >= CHECKMATE - 1024) && (!comment))
	{
		if (((CHECKMATE - abs(nPVEval)) / 2) > 0)	// don't announce "Mate in 0"
		{
			int val = (CHECKMATE - abs(nPVEval) + 1) / 2;

			if (nPVEval < 0)
				val = -val;

			sprintf(moveString, "(Mate in %d) ", val);
			strcat(buf, moveString);
		}
	}
	
	strcat(buf, "\n");

    if (bPost && !bSlave)
        printf(buf);

    if (!bSlave && (bKibitz || bComputer) && bPrintKibitz)
    {
        printf("tellics kibitz %s\n", buf);
        if (nPVEval >= CHECKMATE - 1024)
            if (((CHECKMATE - nPVEval - 1) / 2) > 0)	// don't announce "Mate in 0"
                printf("tellics kibitz Mate in %d\n", (CHECKMATE - nPVEval - 1) / 2);
    }
    if (bLog)
        fprintf(logfile, buf);

#if SHOW_QS_NODES
        printf("%12llu qnodes - %d pct\n", nQNodes, (nQNodes * 100 / nSearchNodes));
#endif

    fflush(stdout);
}

#if USE_SMP
/*========================================================================
** StartProcesses -- starts a number of slave processes for SMP support
**========================================================================
*/
void StartProcesses(int nCPUs)
{
	if ((nCPUs > 1) && !bSlave)
	{
		int		n;
		char	commandline[256];

		// create slave processes
		for (n = 0; n < nCPUs - 1; n++)
		{
			ZeroMemory(&si, sizeof(STARTUPINFO));
			si.cb = sizeof(si);
			ZeroMemory(&pi[n], sizeof(pi[n]));

			sprintf(commandline, "\"%s\" slave sharedmem=%s sharedhash=%s numslave=%d", szProgName, szSharedMemName, szSharedHashName, n);
#if DEBUG_SMP
			printf("Launching slave process %d with '%s'\n", n, commandline);
#endif

			// Start the child process. 
			if( !CreateProcess( NULL,   // No module name (use command line)
				commandline,        // Command line
				NULL,           // Process handle not inheritable
				NULL,           // Thread handle not inheritable
				FALSE,          // Set handle inheritance to FALSE
#if DEBUG_SMP
				CREATE_NO_WINDOW, // CREATE_NEW_CONSOLE,
#else
				CREATE_NO_WINDOW,              // No creation flags
#endif
				NULL,           // Use parent's environment block
				NULL,           // Use parent's starting directory 
				&si,            // Pointer to STARTUPINFO structure
				&pi[n] )        // Pointer to PROCESS_INFORMATION structure
			) 
			{
				printf( "CreateProcess failed (%d).\n", GetLastError() );
			}
		}
	}
}

/*========================================================================
** KillProcesses -- kill all SMP processes on shutdown
**========================================================================
*/
void KillProcesses(void)
{
	// this is just in case the program was terminated via the dos box
	int n;

	for (n = 0; n < nCPUs - 1; n++)
	{
        TerminateProcess(pi[n].hProcess, 0); // Don't use force, just get a bigger hammer.
		CloseHandle(pi[n].hThread);
	}
}
#endif

void cleanup(void)
{
	if (bLog)
	{
		fflush(logfile);
		fclose(logfile);
	}
}

/*========================================================================
** main - initialize and main loop handling console input/output and calls
** to search
**========================================================================
*/
#if USE_SMP
int main(int argc, char *argv[])
#else
int main(void)
#endif
{
#if USE_SMP
	atexit(KillProcesses);	// just in case the program was shut down via the dos box, this will cover all exit cases
#endif

    bLog = bKibitz = FALSE;

#if USE_SMP
	strcpy(szProgName, argv[0]);
	ParseCommandline(argc, argv);
#if DEBUG_SMP
	if (bSlave)
		printf("This is slave number %d\n", nSlaveNum);
#endif
#endif
    ParseIniFile();

	atexit(cleanup);

    if (bLog)
    {
        char	fn[32];
		_timeb  tb;

        _mkdir("logs");
		_ftime(&tb);
		sprintf(fn, "logs\\Myrddin-%lld-%d-%s%d.log", (long long) tb.time, tb.millitm, (bSlave ? "slave" : ""), nSlaveNum+1);
        logfile = fopen(fn, "w+");

        if (!logfile)
            bLog = FALSE;
		else
		{
			fprintf(logfile, "%s - %-14s\n", szVersion, szInfo);
			fprintf(logfile, "log=%d, kibitz=%d, hashsize=%lld, egtbcomp=%d, egtbfolder=%s, cores=%d\n", bLog, bKibitz, dwHashSize, nEGTBCompressionType, szEGTBPath, nCPUs);
		}
    }

    bPost = TRUE;		// only for debugging convenience, should be OFF by default, but most engines seem to do this anyway
    bPondering = FALSE;	// no default is specified in the Winboard spec, but this is less annoying to users, I guess
    bXboard = FALSE;	// assume we're not using a Winboard interface until told

    // default time management values if we don't get anything else
    nThinkTime = 1000;		// think for one second unless we're told otherwise
    nCheckNodes = 0x7FFF;	// every 32K nodes

    fflush(stdin);

	if (!bSlave)
	{
		printf("\n");
		printf("#-------------------------------#\n");
		printf("# %-13s - %-13s #\n", szVersion, szInfo);
		printf("# Copyright 2023 - John Merlino #\n");
		printf("# All Rights Reserved           #\n");
		printf("#-------------------------------#\n\n");
		printf("feature done=0\n");	// just in case -- this shouldn't be harmful according to Tim Mann
	}

#if USE_HASH
    if (InitHash() == NULL)
    {
        printf("Unable to allocate hash table...exiting\n");
		fprintf(logfile, "Unable to allocate hash table...exiting\n");
        return(0);
    }
#endif

#if USE_SMP
	StartProcesses(nCPUs);
#if DEBUG_SMP
	printf("Launching %d process%s\n", nCPUs, (nCPUs > 1 ? "es" : ""));
#endif
#endif

    initbitboards();
    InitThink();

	if (nn_load(NN_FILE) == -1)
	{
		printf("Unable to load network data. Cannot continue\n");
		return(0);
	}

	if (!bSlave)
	    INITIALIZE();	// prodeo book

#if USE_EGTB
    if (GaviotaTBInit() == EXIT_FAILURE)
		bNoTB = TRUE;
#else
	tb_available = FALSE;
#endif

    bbNewGame(&bbBoard);

    bStoreCommand = FALSE;
    nEngineMode = ENGINE_IDLE;
    nEngineCommand = NO_COMMAND;
    nCompSide = NO_SIDE;
    bExactThinkTime = FALSE;
    bExactThinkDepth = FALSE;

	if (!bSlave)
	{
		InitializeInput();
		if (bXboard)
			printf("done=1\n");

#if USE_SMP
		int	x;

		// clear out slave communication data and set it to locked to wait for slaves to be ready
		for (x = 0; x < nCPUs - 1; x++)
		{
			ZeroMemory(&smSharedMem->sdSlaveData[x], sizeof(SLAVE_DATA));
			smSharedMem->sdSlaveData[x].bLocked = TRUE;
		}
#endif
	}
#if USE_SMP
	else
		smSharedMem->sdSlaveData[nSlaveNum].bLocked = FALSE;
#endif

    PromptForInput();

    // main engine/input loop
    for (;;)
    {
		if (!bSlave)
			fflush(stdout);
		else
			nCompSide = NO_SIDE;

        if (nEngineMode == ENGINE_ANALYZING)
            nCompSide = bbBoard.sidetomove;	// when analyzing, assume the engine is always on the move

        if (nCompSide == bbBoard.sidetomove)
        {
            char	moveString[12];
            int  	nEval;

            // time to choose a move

            // get book move?
            *FROM = '\0';

            // book depth is max 60 plies
            if ((nEngineMode != ENGINE_ANALYZING) && (nEngineCommand != PONDER) && (nGameMove < 60) && (!bSlave))
            {
                BBBoardToForsythe(&bbBoard, 0, EPD);
                FIND_OPENING();
            }
            if (*FROM)	// we've got a book move
            {
                CHESSMOVE	cmTempMoveList[MAX_LEGAL_MOVES];
                char		bookString[8];
                WORD		nNumMoves;
                int			n;

                bInBook = TRUE;
                sprintf(bookString, "%s%s", strlwr(FROM), strlwr(TO));

                BBGenerateAllMoves(&bbBoard, cmTempMoveList, &nNumMoves, FALSE);

                // find the book move in the list of legal moves
                for (n = 0; n < nNumMoves; n++)
                {
                    MoveToString(moveString, &cmTempMoveList[n], FALSE);

                    if (!strnicmp(moveString, bookString, strlen(moveString)))	// we've found our move
                    {
						cmChosenMove = cmTempMoveList[n];

                        MoveToString(moveString, &cmChosenMove, TRUE);
                        printf("\n%s\n", moveString);
#if USE_SMP
						if (!bSlave)
						{
							MoveToString(moveString, &cmChosenMove, FALSE);
							SendSlavesString(moveString);
						}
#endif
                        fflush(stdout);

                        if (bKibitz || bComputer)
                            printf("tellics kibitz book move:%s\n", moveString);

                        if (bLog)
                            fprintf(logfile, "< book %s\n", moveString);

                        BBMakeMove(&cmChosenMove, &bbBoard);
						cmChosenMove.dwSignature = bbBoard.signature;
                        cmGameMoveList[nGameMove++] = cmChosenMove;
                    }
                }
            }
            else	// no book move, time to think
            {
                CHESSMOVE	cmTempMoveList[MAX_LEGAL_MOVES];
                WORD		nNumMoves;
                BOOL		bFoundMate = FALSE;

                // initialize thinking params
                bInBook = FALSE;
                nThinkStart = GetTickCount();
                nPonderTime = 0;
                nDepth = 1;
                nCurEval = nPrevEval = NO_EVAL;
                nSearchNodes = 0;
#if SHOW_QS_NODES
                nQNodes = 0;
#endif
#if USE_SMP
				ZeroSlaveNodes();
#endif

                if (nEngineMode != ENGINE_ANALYZING)
                {
                    if (bLog)
                        fprintf(logfile, "think start, setting to ENGINE_THINKING\n");

                    nEngineMode = ENGINE_THINKING;
                }

                if (nEngineCommand == PONDER)
                {
                    if (bLog)
                        fprintf(logfile, "think start, setting to ENGINE_PONDERING\n");

                    nEngineMode = ENGINE_PONDERING;
                    nCheckNodes = 0x7FFF;   // check often while pondering - about 50x per second
                }

                nEngineCommand = NO_COMMAND;

                BBGenerateAllMoves(&bbBoard, cmTempMoveList, &nNumMoves, FALSE);

#if USE_SMP
				if (!bSlave)
					SendSlavesString("analyze");
#endif

                // iterative depth loop
                do
                {
#if USE_SMP
                    if (bSlave)
                    {
                        if (nSlaveNum & 1)
                            nEval = Think(nDepth + 1);
                        else
                            nEval = Think(nDepth);
                    }
                    else
#endif
					    nEval = Think(nDepth);

                    // we've been told to jump out of the loop, either due to a command or time concerns
                    if ((nEngineCommand == STOP_THINKING) || (nEngineCommand == END_THINKING))
                        break;

                    if ((evalPV.pvLength == 0) && (prevDepthPV.pvLength == 0))	// no legal moves!
                    {
                        if (nEngineMode == ENGINE_PONDERING)
                        {
                            // back out the pondering move
                            if (bLog)
                                fprintf(logfile, "backing out the pondering move because there is no legal reply\n");

							bbBoard = bbPonderRestore;
                            ZeroMemory(&cmGameMoveList[--nGameMove], sizeof(CHESSMOVE));
                        }

                        if (nEngineMode == ENGINE_ANALYZING)
                        {
                            // stop analyzing as there are no legal moves, just loop in place waiting for a command
                            do
                            {
                                printf("<no legal moves ... waiting for command>\n");

                                if (bLog)
                                    fprintf(logfile, "<no legal moves ... waiting for command>\n");

                                CheckForInput(TRUE);
                                HandleCommand();
                                if ((nEngineCommand == STOP_THINKING) || (nEngineCommand == END_THINKING))
                                    break;

                            } while (1);
                        }

                        break;
                    }

                    // there's only one legal reply, so let's not waste time and just play the move
                    if ((nNumMoves == 1) && (nEngineMode == ENGINE_THINKING))
                    {
                        if (!bPondering && (nDepth >= 1))
                            break;	// not pondering, so depth 1 search is enough
                        if (bPondering && (nDepth >= 5))
                            break;	// search to depth 5 when pondering to guarantee a move to ponder on
                    }

                    // check for being near checkmate at the current depth - if we get it twice in a row, just play the move to save time
                    if ((nDepth >= 5) && (nEval >= CHECKMATE - nDepth) && (nEngineMode == ENGINE_THINKING))
                    {
                        if (bFoundMate)
                            break;
                        else
                            bFoundMate = TRUE;
                    }
                    else
                        bFoundMate = FALSE;

                    // if we have TBs and there are <=5 men on the board, just make the move
                    if (tb_available && (BitCount(bbBoard.bbOccupancy) <= 5) && (nEngineMode == ENGINE_THINKING))
                        break;

                    // are we at the requested search depth due to an "sd" command?
                    if (bExactThinkDepth && (nDepth >= nThinkDepth))
                        break;

                    nDepth++;

#if 0 // USE_SMP         // more aggressive depth adjustment for some child processes
                    if (bSlave)
                    {
                        if ((nSlaveNum & 1) == 0)
                            nDepth++;
                    }
#endif

                    if (((nEngineMode == ENGINE_PONDERING) || (nEngineMode == ENGINE_ANALYZING)) && (nDepth > MAX_DEPTH))
                    {
                        // we've reached our maximum depth. If we're on the move, we'll just make a move here. But if not,
                        // then just loop in place until a command/move is sent that bounces us out
                        do
                        {
                            printf("<max search depth reached ... waiting for command>\n");

                            if (bLog)
                                fprintf(logfile, "<max search depth reached ... waiting for command>\n");

                            CheckForInput(TRUE);
                            HandleCommand();
                            if ((nEngineCommand == STOP_THINKING) || (nEngineCommand == END_THINKING) ||
                                    (nEngineMode == ENGINE_THINKING))
                                break;

                        } while (1);
                    }
                }
                while (nDepth <= MAX_DEPTH);

                // we have a move, so play it and update the game board/movelist
                if ((evalPV.pvLength || (nEngineCommand == END_THINKING)) && (nEngineMode == ENGINE_THINKING) &&
                        (nEngineCommand != STOP_THINKING))
                {
                    if ((nEval == INFINITY) || (nEval == -INFINITY))
                        // in case the search for the current depth did not finish with its first move
                        PrintPV(nPrevPVEval, OPPONENT(bbBoard.sidetomove), '\0', TRUE);
                    else
                    {
                        PrintPV(nEval, OPPONENT(bbBoard.sidetomove), '\0', TRUE);
                        nPrevPVEval = nEval;
                    }

                    MoveToString(moveString, &cmChosenMove, TRUE);

                    printf("\n%s\n", moveString);
#if USE_SMP
					if (!bSlave)
					{
						if (!bPondering)
							SendSlavesString("stop");

						MoveToString(moveString, &cmChosenMove, FALSE);
						SendSlavesString(moveString);
					}
#endif
                    fflush(stdout);

                    BBMakeMove(&cmChosenMove, &bbBoard);

                    if (bLog)
                        fprintf(logfile, "< %s, nFifty=%d\n", moveString, bbBoard.fifty);

					cmChosenMove.dwSignature = bbBoard.signature;
                    cmGameMoveList[nGameMove++] = cmChosenMove;

                    // checkmate?
                    if (nEval == (CHECKMATE - 1))
                    {
                        if (bLog)
                            fprintf(logfile, "%s {Checkmate}\n", (nCompSide == WHITE) ? "1-0" : "0-1");

                        printf("%s {Checkmate}\n", (nCompSide == WHITE) ? "1-0" : "0-1");
                    }
                    // check for draw
                    else if (GamePositionRepeated(cmChosenMove.dwSignature))
                    {
                        if (bLog)
                            fprintf(logfile, "1/2-1/2 {Draw by Repetition}\n");

                        printf("1/2-1/2 {Draw by Repetition}\n");
                    }
                    else if (bbBoard.fifty >= 100)
                    {
                        if (bLog)
                            fprintf(logfile, "1/2-1/2 {Draw by 50-move rule}\n");

                        printf("1/2-1/2 {Draw by 50-move rule}\n");
                    }
                    else if (BoardIsMaterialDraw(&bbBoard))
                    {
                        if (bLog)
                            fprintf(logfile, "1/2-1/2 {Draw by Insufficient Material}\n");

                        printf("1/2-1/2 {Draw by Insufficient Material}\n");
                    }
                }

                // there's no move, because the PV length is 0
                if ((evalPV.pvLength == 0) && (prevDepthPV.pvLength == 0) &&
                        (nEngineCommand != END_THINKING) && (nEngineMode != ENGINE_ANALYZING) &&
                        (nEngineCommand != ENGINE_PONDERING))
                {
                    // no legal moves, game over (stalemate or checkmate)
                    if (BBKingInDanger(&bbBoard, nCompSide))
                        printf("I lost\n");
                    else
                        printf("1/2-1/2 {Stalemate}\n");

                    fflush(stdout);
                    nCompSide = NO_SIDE;	// tells the engine to stop
                }
            }

            if (bLog)
                fprintf(logfile, "Before Pondering Prep -- nEngineMode == %d, nEngineCommand = %d\n", nEngineMode, nEngineCommand);

            if (bPondering && !bInBook && (nEngineMode != ENGINE_PONDERING) && (nEngineMode != ENGINE_ANALYZING) &&
                    (nCompSide != NO_SIDE))
            {
                // prep for pondering
                CHESSMOVE	cmTempMoveList[MAX_LEGAL_MOVES];
                WORD		nNumMoves;

                // make the expected reply on the game board (if there is an expected reply)
				if (evalPV.pvLength > 1)
				{
					cmPonderMove.fsquare = evalPV.pv[1].fsquare;
					cmPonderMove.tsquare = evalPV.pv[1].tsquare;
					cmPonderMove.moveflag = evalPV.pv[1].moveflag;
				}
                else
                {
                    // check to see if the previous PV has the same move AND has an expected reply
                    if (prevDepthPV.pvLength > 1)
                    {
                        if ((evalPV.pv[0].fsquare == prevDepthPV.pv[0].fsquare) &&
                                (evalPV.pv[0].tsquare == prevDepthPV.pv[0].tsquare))
                        {
                            cmPonderMove.fsquare = prevDepthPV.pv[1].fsquare;
							cmPonderMove.tsquare = prevDepthPV.pv[1].tsquare;
							cmPonderMove.moveflag = prevDepthPV.pv[1].moveflag;
						}
                    }
                    else
                    {
                        if (bLog)
                            fprintf(logfile, "Bailing on pondering as there is no expected reply in the PV\n");

                        goto NoPonder;	// the PV has only one move in it, so there is no expected reply -- BAIL!
                    }
                }

				// save off the board so it can be restored after pondering is finished
				bbPonderRestore = bbBoard;

                BBMakeMove(&cmPonderMove, &bbBoard);
                cmPonderMove.dwSignature = bbBoard.signature;
                cmGameMoveList[nGameMove++] = cmPonderMove;

                if (bLog)
                    fprintf(logfile, "Pondering on %s\n", MoveToString(moveString, &cmPonderMove, FALSE));

                // don't ponder if there are no legal replies to the expected move (e.g. checkmate)
                BBGenerateAllMoves(&bbBoard, cmTempMoveList, &nNumMoves, FALSE);

                if (nNumMoves)
                    nEngineCommand = PONDER;
                else
                {
                    // back out the ponder move
					bbBoard = bbPonderRestore;
					ZeroMemory(&cmGameMoveList[--nGameMove], sizeof(CHESSMOVE));
                }
            }

NoPonder:
            if ((nEngineMode != ENGINE_ANALYZING) && (nEngineCommand != PONDER))
            {
                if (bLog)
                    fprintf(logfile, "Engine holding...\n");

                PromptForInput();
                nEngineMode = ENGINE_IDLE;
                nEngineCommand = NO_COMMAND;
            }
        }

        if (bStoreCommand)
            HandleCommand();

        if ((nEngineMode != ENGINE_ANALYZING) && (nEngineCommand != PONDER) && (bbBoard.sidetomove != nCompSide))
        {
            if (bLog)
                fprintf(logfile, "waiting for input\n");

            CheckForInput(TRUE);
            HandleCommand();
        }
    }
}
