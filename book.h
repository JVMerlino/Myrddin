void INITIALIZE(void);
void FIND_OPENING(void);
void MAKE_HASHKEY(void);
void BOEKIN(void);
void BOEK_BEGIN(void);

int test_prodeo_book(void);

static char BORDPOS[] =
    {    8,18,28,38,48,58,68,78,
         7,17,27,37,47,57,67,77,
         6,16,26,36,46,56,66,76,
         5,15,25,35,45,55,65,75,
         4,14,24,34,44,54,64,74,
         3,13,23,33,43,53,63,73,
         2,12,22,32,42,52,62,72,
         1,11,21,31,41,51,61,71,0
    };

static char BVELD[] = {  1,11,21,31,41,51,61,71,	// MVS format to
                         2,12,22,32,42,52,62,72,	// REBEL format
                         3,13,23,33,43,53,63,73,
                         4,14,24,34,44,54,64,74,
                         5,15,25,35,45,55,65,75,
                         6,16,26,36,46,56,66,76,
                         7,17,27,37,47,57,67,77,
                         8,18,28,38,48,58,68,78
                      };

extern char EPD[];
extern char FROM1[],FROM2[],TO1[],TO2[];
extern char FROM[10],TO[10];

static char TA[] = {                                // ASCII output REBEL moves
    'X',
    'A','A','A','A','A','A','A','A','X','X',
    'B','B','B','B','B','B','B','B','X','X',
    'C','C','C','C','C','C','C','C','X','X',
    'D','D','D','D','D','D','D','D','X','X',
    'E','E','E','E','E','E','E','E','X','X',
    'F','F','F','F','F','F','F','F','X','X',
    'G','G','G','G','G','G','G','G','X','X',
    'H','H','H','H','H','H','H','H'
};

static char TN[] = {			                    // ASCII output REBEL moves
    'X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8','X','X',
    '1','2','3','4','5','6','7','8'
};

static char BOEKRND[] = { 8,4,2,1,1,1,1,1,1,1,1,1,1 };