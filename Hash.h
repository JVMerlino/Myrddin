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

#pragma pack(push,1)
typedef struct hash_item
{
    PosSignature	dwSignature;
    short			nEval;
    MoveFlagType	moveflag;
    BYTE			nFlags;
    BYTE			nDepth;
    SquareType		from;
    SquareType		to;
} hash_item;

typedef union HASH_ENTRY {
	hash_item h;
	long long  l[2];
} HASH_ENTRY;

typedef struct EVAL_HASH_ENTRY
{
    PosSignature	dwSignature;
    short			nEval;
} EVAL_HASH_ENTRY;
#pragma pack(pop)

#define DEFAULT_HASH_SIZE		(0x800000)	// 128MB

#define HASH_NOT_EVAL		(0x00)
#define HASH_ALPHA			(0x10)
#define HASH_BETA			(0x20)
#define HASH_EXACT			(0x40)
#define HASH_MATE_THREAT	(0x01)

extern size_t	dwHashSize;

HASH_ENTRY *InitHash(void);
void		ClearHash(void);
void		CloseHash(void);

void		SaveHash(CHESSMOVE *cmMove, int nDepth, int nEval, BYTE nFlags, int nPly, PosSignature dwSignature);
HASH_ENTRY *ProbeHash(PosSignature dwSignature);

extern void SaveEvalHash(int nEval, PosSignature dwSignature);
extern EVAL_HASH_ENTRY *	ProbeEvalHash(PosSignature dwSignature);

PosSignature	GetBBSignature(BB_BOARD *bbBoard);
