/*                      Opening Book Module for Chess Programs
                                written by Ed Schröder
                                    Version 1.00

           This information provides the way how to include the Pro Deo 1.4 opening
           book (author Jeroen Noomen) into your chess engine.

           Include BOOK.C into your project and compile.

           MAINBOOK.BIN   - The big mainbook.
           TOURBOOK.BIN   - A selection of favoured opening lines called the Tournament Book.

           Initialization - Call the routine "INITIALIZE()" once.

           Operation      - Create an EPD string from the current position and strcpy it into
                            the string "EPD", then call the routine "FIND_OPENING()" to search
                            the opening book.

                          - When a move is found it is stored into the strings FROM and TO
                            in Ascii format, so E2 and E4, else the strings are empty. Play
                            this move. That's basically it.

                          - Alternatively you can also make a choice yourself from the list
                            of found opening moves, see: FROM1,FROM2,TO1,TO2 and AZ. See the
                            demonstration code in the "main" section.

           Remarks        - Although not recommended it's possible to turn off the Tournament
                            Book and play from the (very varied) big Main Book only. Just
                            empty the variable "TOERFILE" before you call "INITIALIZE()".

                          - To create an EPD string from the current board position check
                            chapter 16 from: http://www.tim-mann.org/Standard
                            Castling status and en-passant information are not needed to
                            include in the EPD string, the module only needs the current
                            position and the colour to move.

                          - As far as I remember the longest book line is 30 moves so out of
                            speed reasons don't call the opening book after move 30.

           History        - The opening book is the intellectual work of Jeroen Noomen who
                            started his work in 1989 for the Mephisto MM4 module from
                            Hegener & Glaser which later became the base of the REBEL series.
                            The book is the result of 15 years diligent hand-typed work.

                            The book was created and maintained by the TASC ChessMachine
                            software so there currently is no neat way to modify the
                            opening book.

           Future work    - For the next version I plan to add a simple book learn algorithm
                            that avoids your program to lose every time on the same book
                            opening line.

                          - Also planned is a simple way to fix possible holes in the book
                            and/or to add new opening lines.

                          - But what is really needed is an new editor and I feel not called.

           Technical      - Short description of the book format:

                            Each book move has 2 bytes

                             Byte1:  - bit 0-5  square FROM (a1,b1,c1 etc.)
                                     - bit 6    there is another subtree available
                                     - bit 7    end of a variation

                             Byte2:  - bit 0-5  square TO (a1,b1,c1 etc.)
                                     - bit 6    unused
                                     - bit 7    good or bad move (play or not play)

           Copyright      - This file and its components although distributed as freeware in
                            no way may become subject to any form of commerce. Permission
                            must be asked first.

                          - This file and its components come as is and can only be used as
                            such. It's forbidden to change, add, delete any of its components
                            and only can be distributed in original form.


           Questions      - For questions, suggestions, bug reports etc. contact me at the
                            CCC forum: http://216.25.93.108/forum

           Ed Schröder
           Deventer, March 2007
           matador@home.nl
           www.top-5000.nl
*/

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <windows.h>
#include <time.h>
#include "Myrddin.h"
#include "book.h"

char BOOKFILE[300]="mainbook.bin";
char TOERFILE[300]="tourbook.bin";
char EPD[200]="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
char FROM[10],TO[10];
char ERR=0;
long RANDOM1[1120];
long RANDOM2[1120];
char _BORD[256];
long *p_BORD = (long *) _BORD;               // 32 bit redefinition BORD
long HK1,HK2;
int  XD,XE,AZ,RND;
char FROM1[20],FROM2[20],TO1[20],TO2[20];
unsigned char _GB[100000];                   // TOERBOEK.BIN
int  BOEKSTAT[20];

FILE *FPB;

void INITIALIZE(void);
void FIND_OPENING(void);
void MAKE_HASHKEY(void);
void BOEKIN(void);
void BOEK_BEGIN(void);


/*========================================================================
** test_main -- verify that everything is working properly
**========================================================================
*/
int test_prodeo_book(void)
{
    int x /* ,d,e */;                                                          // DEMONSTRATION

//        strcpy(TOERFILE,"");	// tourbook.bin                                  // tournament book to search
//        strcpy(BOOKFILE,"Myrddin.bin");                                     // main book to search

//        INITIALIZE();                                                       // Initialize (one time call)
    if (ERR)
    {
        printf("Missing obliged file(s) RANDOM1.BIN and/or RANDOM2.BIN");
        exit(1);
    }

    strcpy(EPD,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"); // start position
    FIND_OPENING();
    if (ERR)
        printf("Something went wrong, error-code %d",ERROR);
    else
        printf("Move: %s-%s\nList: ",FROM,TO);

    for (x=0; x<AZ; x++)
        printf("%c%c-%c%c ",FROM1[x],FROM2[x],TO1[x],TO2[x]);
    printf("\n\n");

    strcpy(EPD,"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3");  // position after 1.e4
    FIND_OPENING();
    printf("Search after 1. e4\nMove: %s-%s\nList: ",FROM,TO);
    for (x=0; x<AZ; x++)
        printf("%c%c-%c%c ",FROM1[x],FROM2[x],TO1[x],TO2[x]);
    printf("\n\n");

    strcpy(EPD,"rnbqkbnr/pppp1ppp/8/3p4/3P4/2N5/PPPP1PPP/R1BQKBNR w KQkq e6"); // position after 1.e4 e5
    FIND_OPENING();
    printf("Search after 1.e4 e5\nMove: %s-%s\nList: ",FROM,TO);
    for (x=0; x<AZ; x++)
        printf("%c%c-%c%c ",FROM1[x],FROM2[x],TO1[x],TO2[x]);
    printf("\n\n");

    return(0);
}


/*========================================================================
** INITIALIZE -- must call at engine startup
**========================================================================
*/
void INITIALIZE(void)                                              // read hashkeys in memory

{
    int x,y /*,r */;
    FILE *fp1;
    FILE *fp2;

    strcpy(TOERFILE,"tourbook.bin");                                    // tournament book to search
    strcpy(BOOKFILE,"mainbook.bin");                                    // main book to search        ERR=0;

    fp1=fopen("random1.bin","rb");
    if (fp1==NULL)
    {
        ERR=1;
        return;
    }

    fp2=fopen("random2.bin","rb");
    if (fp2==NULL)
    {
        ERR=1;
        return;
    }

    for (x=0; x<1120; x++)
    {
        fread(&RANDOM1[x],4,1,fp1);
        fread(&RANDOM2[x],4,1,fp2);
    }

    fclose(fp1);
    fclose(fp2);

    fp1=fopen(TOERFILE,"rb");                             // read tournament book in memory
    if (fp1==NULL)
    {
        _GB[0]=255;
        _GB[1]=255;
        return;
    }

    y=0;

    while ((x=getc(fp1))!=EOF)
    {
        _GB[y]=x;
        y++;
    }
	INDEX_CHECK(y-1, _GB);
    _GB[y-1]=255;
	INDEX_CHECK(y-2, _GB); 
	_GB[y-2]=255;
    fclose(fp1);
}


/*========================================================================
** FIND_OPENING -- takes FEN string from global EPD and returns characters
** for from and to squares
**========================================================================
*/
void FIND_OPENING(void)                                 // input : BOOKFILE / EPD
// output: FROM and TO as E2 and E4 and ERROR
{
    int xl,px,xx,x,y,rnd,bhk1,bhk2,xd,xe;
    int ch,by,v,kleur,boekfase;
    char db[100],eb[100];
    unsigned char zcd[200],zce[200],vb[100];
    char fentab[]   = "??PNBRQKpnbrqk??";
    char fenkleur[] = "wb??";
    clock_t a;

    ERR=0;

    if ((FPB = fopen(BOOKFILE,"rb")) == NULL)
    {
        ERR=2;
        return;
    }

    FROM[0]=0;
    TO[0]=0;

    db[0]=0;
    eb[0]=0;
    vb[0]=255;

    RND=clock();                                // initialize randomizer
    a=clock();
    srand(clock());
    x=rand();
    rnd=RND+x;

    RND=rnd;                                    // debug info

    AZ=0;                                       // number of found book moves

    boekfase=0;                                 // search tournament book first
    if (_GB[0]==255)
        boekfase=1;                // no tournament book selected -> search main book

//      retrieve EPD
//      ============

    for (x=1; x<=78; x++)
    {
        if (_BORD[x] > 0)
            _BORD[x]=1;     // empty board
    }

    x=-1;
    y=0;

next:
    x++;
    ch=EPD[x];
    if (ch=='/')
        goto next;

    if (ch >= '1' && ch <= '8')
    {
        v=ch-48;
        y=y+v;
        goto test;
    }

    for (v=2; v<=15; v++)
    {
        if (fentab[v]==ch)
        {
            _BORD[BORDPOS[y]]=v;
            y++;
            break;
        }
    }

    if (v>13)
        goto error;

test:
    if (y < 64)
        goto next;

    x++;
    if (EPD[x] != ' ')
        goto error;

    x++;

    if (EPD[x]==fenkleur[0])
    {
        kleur=0;
        goto done;
    }   // white to move

    if (EPD[x]==fenkleur[1])
    {
        kleur=1;
        goto done;
    }   // black to move

error:
    ERR=3;
    fclose(FPB);
    return;

done:
    MAKE_HASHKEY();
    bhk1=HK1;
    bhk2=HK2;                     // make hashkey of retrieved EPD

again:
    xl=-2;
    px=-1;

boek10:
    px++;
boek12:
    if (boekfase)                                       // read main book
    {
        if ((xx = fgetc(FPB)) == EOF)
            goto boek90;

        zcd[px]=xx;
        xl=xl+2;

        if ((xx = fgetc(FPB)) == EOF)
            goto boek90;

        zce[px]=xx;
    }
    else
    {
        xl=xl+2;
        if (_GB[xl]==255 && _GB[xl+1]==255)
            goto boek90;  // read tournament book

        zce[px]=_GB[xl+1];
        zcd[px]=_GB[xl];
    }

    if (zcd[px] < 128)
        goto boek10;                     // collect moves till end of varition

//      Search the filled stack for possible bookmove
//      =============================================

    x=0;
    BOEK_BEGIN();
    HK1=0x05035c45;
    HK2=0xf1b92b1f;  // hashkey values start position

boek14:
    if (bhk1==HK1 && bhk2==HK2)
        goto boek16;            // hashkeys match, bookmove found

    XD=BVELD[zcd[x] & 0x3f];
    XE=BVELD[zce[x] & 0x3f];
    BOEKIN();                                           // update board and hashkeys

    x++;
    if (x > px)
        goto boek20;                       // end of variantion, check for next

    goto boek14;

//      Bookmove found, insert in table
//      ===============================

boek16:
    if (zce[x]<64)
        goto boek20;                         // Bad book move, no insert
    if ((x&1) != (kleur&1))
        goto boek20;                // colours don't match, no insert

    xd=BVELD[zcd[x]&0x3f];
    xe=BVELD[zce[x]&0x3f];       // Bookmove found

    for (v=0; v<AZ; v++)                                // skip possible doubles
    {
        if (db[v]==xd && eb[v]==xe && vb[v]<64)
            vb[v]=zce[x];
        if (db[v]==xd && eb[v]==xe)
            goto boek20;
    }

    db[AZ]=xd;
    eb[AZ]=xe;
    vb[AZ]=zce[x];                // store move in internal format

    FROM1[AZ]=TA[xd];
    FROM2[AZ]=TN[xd];                 // store move in ascii format

    TO1[AZ]=TA[xe];
    TO2[AZ]=TN[xe];

    BOEKSTAT[AZ]=boekfase;
    AZ++;                                               // number of found book moves

//      Spool back till bit-6 becomes zero
//      ==================================

boek20:
    if (zcd[px] < 192)
        goto boek12;                         // there is another one !

boek22:
    px--;
    if (px < 0)
        goto boek10;
    if (zcd[px] >= 64)
        goto boek22;
    goto boek12;

//      End of File, decide move to play
//      ================================

boek90:
    if (boekfase==0 && AZ==0)
    {
        boekfase=1;
        goto again;
    }   // nothing found in tournament book
    // search now the main book
    if (AZ==0)
        goto einde;                                  // no book move(s) found

boek92:
    rnd=rnd & 63;
    by=-1;

boek94:
    by++;
    if (by >= AZ)
        goto boek92;

    rnd=rnd-BOEKRND[by];
    if (rnd > 0)
        goto boek94;

    x=db[by];
    FROM[0]=TA[x];
    FROM[1]=TN[x];
    FROM[2]=0;

    x=eb[by];
    TO[0]=TA[x];
    TO[1]=TN[x];
    TO[2]=0;

einde:
    fclose(FPB);
}

/*========================================================================
** BOEK_BEGIN
**========================================================================
*/
void BOEK_BEGIN(void)                                               // start position to BORD
{
    p_BORD[0]=0x01020500;           // a0/a1/a2/a3
    p_BORD[1]=0x08010101;           // a4/a5/a6/a7
    p_BORD[2]=0x0300000b;           // a8/a9/a0/b1
    p_BORD[3]=0x01010102;           // b2/b3/b4/b5
    p_BORD[4]=0x00090801;           // b6/b7/b8/b9

    p_BORD[5]=0x01020400;           // b0/c1/c2/c3
    p_BORD[6]=0x08010101;           // c4/c5/c6/c7
    p_BORD[7]=0x0600000a;           // c8/c9/c0/d1
    p_BORD[8]=0x01010102;           // d2/d3/d4/d5
    p_BORD[9]=0x000c0801;           // d6/d7/d8/d9

    p_BORD[10]=0x01020700;          // d0/e1/e2/e3
    p_BORD[11]=0x08010101;          // e4/e5/e6/e7
    p_BORD[12]=0x0400000d;          // e8/e9/e0/f1
    p_BORD[13]=0x01010102;          // f2/f3/f4/f5
    p_BORD[14]=0x000a0801;          // f6/f7/f8/f9

    p_BORD[15]=0x01020300;          // f0/g1/g2/g3
    p_BORD[16]=0x08010101;          // g4/g5/g6/g7
    p_BORD[17]=0x05000009;          // g8/g9/g0/h1
    p_BORD[18]=0x01010102;          // h2/h3/h4/h5
    p_BORD[19]=0x000b0801;          // h6/h7/h8/h9
}

/*========================================================================
** MAKE_HASHKEY
**========================================================================
*/
void MAKE_HASHKEY(void)

{
    int x,y,v,sc;

    HK1=0;
    HK2=0;

    for (x=1; x<=78; x++)
    {
        if (_BORD[x]<2)
            continue;

        sc=_BORD[x];
        v=sc<<6;
        y=sc<<4;
        y=y+v;

        HK1=HK1 ^ RANDOM1[y+x];
        HK2=HK2 ^ RANDOM2[y+x];
    }
}


/*========================================================================
** BOEKIN
**========================================================================
*/
void BOEKIN(void)                                                   // update BORD and HASHKEY

{
    int sc,z,y0;

    char kntab[] = { 0,0,2,0,0,0,0,1,3,0,0,0,0,1 };

    sc=_BORD[XD];
    y0=_BORD[XE];

    z=sc<<6;
    z=z+(sc<<4);

    HK1=HK1 ^ RANDOM1[z+XD];
    HK1=HK1 ^ RANDOM1[z+XE];
    HK2=HK2 ^ RANDOM2[z+XD];
    HK2=HK2 ^ RANDOM2[z+XE];

    _BORD[XE]=sc;
    _BORD[XD]=1;

    if (y0!=1)                                              // remove captured piece from hashkey
    {
        z=y0<<6;
        z=z+(y0<<4);

        HK1=HK1 ^ RANDOM1[z+XE];
        HK2=HK2 ^ RANDOM2[z+XE];
    }

    if (kntab[sc]==1)                                       // handle castling
    {
        if (XE-XD==20)
        {
            _BORD[XE+10]=1;
            _BORD[XE-10]=sc-2;
            MAKE_HASHKEY();
        }

        if (XE-XD==-20)
        {
            _BORD[XE-20]=1;
            _BORD[XE+10]=sc-2;
            MAKE_HASHKEY();
        }
    }

    if (sc==2 && y0==1 && XE-XD!=1 && XE-XD!=2)
    {
        _BORD[XE-1]=1;
        MAKE_HASHKEY();
    }    // ep (white)

    if (sc==8 && y0==1 && XD-XE!=1 && XD-XE!=2)
    {
        _BORD[XE+1]=1;
        MAKE_HASHKEY();
    }    // ep (black)
}
