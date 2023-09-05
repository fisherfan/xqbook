#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#pragma comment(lib,"libdb181d.lib")

typedef unsigned char bool;
#define true 1
#define false 0

typedef struct XQKEY
{
	unsigned char Key[128];
	int KeyLen;
	bool MirrorUD;//是否上下镜像了
	bool MirrorLR;//是否左右镜像了
	int Rows;//棋盘有多少行
	int Cols;//棋盘有多少列
} XQKEY;

typedef struct BOOKITEM
{
	__int64 Id;
	unsigned short Move;
	int Score;
	int Win;
	int Draw;
	int Lost;
	bool Valid;
	char Memo[64];
} BOOKITEM;

int GetRowsAndCols(const char *fen, int *rows, int *cols)
{
	*rows = 1;
	*cols = 0;
	bool calcCols = true;
	for (int i = 0; fen[i] != ' '; i++)
	{
		if (fen[i] == '/')
		{
			(*rows)++;
			calcCols = false;
		}
		else if (calcCols)
		{
			*cols += (fen[i] >= '0' && fen[i] <= '9') ? fen[i] - '0' : 1;
		}
	}
	return *rows * *cols;
}

void FenToKey(const char *fen, XQKEY *xqKey)
{
	//fen转成数组
	int turn = *(strchr(fen, ' ') + 1) != 'b';//走棋顺序，0黑1红
	int size = GetRowsAndCols(fen, &xqKey->Rows, &xqKey->Cols);
	char *ary = malloc(size);
	memset(ary, -1, size);//用-1表示空格子，因为0用来表示暗子了
	for (int i = 0, index = 0; fen[i] != ' ' && index < size; i++)
	{
		if (fen[i] >= '0' && fen[i] <= '9')
		{
			index += fen[i] - '0';
		}
		else if(fen[i]!='/')
		{
			char val = -1;
			switch (turn == 0 ? (fen[i] ^ 0x20) : fen[i])//注意这里，如果走棋顺序是黑方的话转换大小写（即大写永远表示走棋方，小写表示不走棋方）
			{
			case 'X':
			case 'x':val = 0; break;//双方暗棋用同一编码，因为上一步转换了大小写而翻翻棋不能区分暗棋颜色（揭棋可分可不分），如果将来有必须区分暗棋颜色的棋种则不能用同一编码
			case 'R':val = 1; break;
			case 'N':val = 2; break;
			case 'B':val = 3; break;
			case 'A':val = 4; break;
			case 'K':val = 5; break;
			case 'C':val = 6; break;
			case 'P':val = 7; break;
			case 'r':val = 9; break;
			case 'n':val = 10; break;
			case 'b':val = 11; break;
			case 'a':val = 12; break;
			case 'k':val = 13; break;
			case 'c':val = 14; break;
			case 'p':val = 15; break;
			}
			ary[index++] = val;
		}
	}
	//上下镜像（如果需要的话）
	xqKey->MirrorUD = false;
	if (turn == 0)//走棋方是黑方的话需要上下镜像
	{
		for (int row = 0; row < xqKey->Rows / 2; row++)
		{
			for (int col = 0; col < xqKey->Cols; col++)
			{
				int index = row * xqKey->Cols + col;
				int index2 = (xqKey->Rows - 1 - row) * xqKey->Cols + col;
				char tmp = ary[index];
				ary[index] = ary[index2];
				ary[index2] = tmp;
			}
		}
		xqKey->MirrorUD = true;
	}
	//左右镜像（如果需要的话）
	xqKey->MirrorLR = false;
	for (int row = 0; row < xqKey->Rows && !xqKey->MirrorLR; row++)
	{
		for (int col = 0; col < xqKey->Cols / 2 && !xqKey->MirrorLR; col++)
		{
			int index = row * xqKey->Cols + col;
			int index2 = row * xqKey->Cols + (xqKey->Cols - 1 - col);
			if (ary[index2] > ary[index])//右边比左边大的话需要左右镜像
				xqKey->MirrorLR = true;
		}
	}
	if (xqKey->MirrorLR)
	{
		for (int row = 0; row < xqKey->Rows; row++)
		{
			for (int col = 0; col < xqKey->Cols / 2; col++)
			{
				int index = row * xqKey->Cols + col;
				int index2 = row * xqKey->Cols + (xqKey->Cols - col - 1);
				char tmp = ary[index];
				ary[index] = ary[index2];
				ary[index2] = tmp;
			}
		}
	}
	//计算key
	xqKey->KeyLen = 0;
	unsigned int buffer = 0;//缓冲区
	const int bufferBits = sizeof(buffer) * 8;//缓冲区里一共有多少位（固定值）
	const int codeBits = 4;//棋子编码位数（固定值）
	int bits = 0;//缓冲区里已保存的位数
	for (int index = 0; index < size; index++)
	{
		if (ary[index] == -1)//无棋直接用0（占1位）
		{
			bits++;
		}
		else//有棋用1+棋子编码（占1+4=5位）
		{
			buffer |= 1 << (bufferBits - bits - 1);
			buffer |= ary[index] << (bufferBits - bits - 1 - codeBits);
			bits += 1 + codeBits;
		}
		if (bits >= bufferBits / 2)//缓冲区里的内容达到一半则写入
		{
			for (int i = 8; i <= bufferBits / 2; i += 8)
				xqKey->Key[xqKey->KeyLen++] = buffer >> (bufferBits - i);
			buffer <<= bufferBits / 2;
			bits -= bufferBits / 2;
		}
	}
	if (bits > 0)//把缓冲区里剩余的内容写入
	{
		for (int i = 8; i < bits + 8; i += 8)
			xqKey->Key[xqKey->KeyLen++] = buffer >> (bufferBits - i);
	}

	free(ary);
}

unsigned short MirrorMove(unsigned short move, bool mirrorUD, bool mirrorLR, int rows, int cols)
{
	if (mirrorUD || mirrorLR)
	{
		int fromRow = move >> 12, fromCol = (move >> 8) & 0xf, toRow = (move >> 4) & 0xf, toCol = move & 0xf;
		if (mirrorUD)
		{
			fromRow = rows - 1 - fromRow;
			toRow = rows - 1 - toRow;
		}
		if (mirrorLR)
		{
			fromCol = cols - 1 - fromCol;
			toCol = cols - 1 - toCol;
		}
		move = (fromRow << 12) | (fromCol << 8) | (toRow << 4) | toCol;
	}
	return move;
}

bool BookInsert(const char *bookName, const XQKEY *xqKey, const BOOKITEM *bookItem)
{
	bool success = false;
	DB *dbp;
	db_create(&dbp, NULL, 0);
	if (dbp->open(dbp, NULL, bookName, NULL, DB_BTREE, DB_CREATE, 0664) == 0)
	{
		DBT key = {0};
		DBT data = {0};
		key.data = xqKey->Key;
		key.size = xqKey->KeyLen;
		unsigned char *pData = malloc(4096);
		if (dbp->get(dbp, NULL, &key, &data, 0) == 0)
		{
			memcpy(pData, data.data, data.size);
			//释放data？
		}
		data.size += BookItemToData(bookItem, pData + data.size);
		dbp->put(dbp, NULL, &key, &data, 0);
		free(pData);
		dbp->close(dbp, 0);
	}
	return success;
}

int BookQuery(const char *bookName, const XQKEY *xqKey, BOOKITEM *bookItems)
{
	int count = 0;
	DB *dbp;
	db_create(&dbp, NULL, 0);
	if (dbp->open(dbp, NULL, bookName, NULL, DB_BTREE, DB_CREATE, 0664) == 0)
	{
		DBT key = {0};
		DBT data = {0};
		key.data = xqKey->Key;
		key.size = xqKey->KeyLen;
		if (dbp->get(dbp, NULL, &key, &data, 0) == 0)
		{
			count = DataToBookItems(data.data, data.size, bookItems);
			//释放data？
		}
		dbp->close(dbp, 0);
	}
	return count;
}

int main(int argc, char **argv)
{
	const char *bookName = "book.xqb";

	//单条插入测试
	//XQKEY xqKey;
	//FenToKey("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1", &xqKey);//初始局面
	//BOOKITEM bookItem = { 0 };
	//bookItem.Move = 0x6656;//兵三进一（6行6列->5行6列）
	//BookInsert(bookName, &xqKey, &bookItem);

	//批量插入测试
	/*FILE *file = fopen("1+2+3mvs.txt", "r");
	if (file)
	{
		char line[256];
		while (fgets(line, sizeof(line), file))
		{
			char *move = strchr(line, ',') + 1;
			char *score = strchr(move, ',') + 1;
			XQKEY xqKey;
			FenToKey(line, &xqKey);
			BOOKITEM bookItem = { 0 };
			bookItem.Move = (xqKey.Rows - 1 - move[1] + '0' << 12) | (move[0] - 'a' << 8) 
				| (xqKey.Rows - 1 - move[3] + '0' << 4) | (move[2] - 'a');
			bookItem.Score = atoi(score);
			BookInsert(bookName, &xqKey, &bookItem);
		}
		fclose(file);
	}*/

	//查询测试
	const char* fens[] = 
	{ 
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1",//初始局面
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C2C4/9/RNBAKABNR b - - 0 1",//炮二平五局面
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/4C2C1/9/RNBAKABNR b - - 0 1",//炮八平五局面（测试左右镜像）
		"rnbakabnr/9/1c2c4/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1",//黑先，炮８平５局面（测试上下镜像）
		"rnbakabnr/9/4c2c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1" //黑先，炮２平５局面（测试上下+左右镜像）
	};
	for (int i = 0; i < sizeof(fens) / sizeof(fens[0]); i++)
	{
		XQKEY xqKey;
		FenToKey(fens[i], &xqKey);
		BOOKITEM bookItems[128];
		int count = BookQuery(bookName, &xqKey, bookItems);
		printf("%s%s",fens[i]," : ");
		for (int j = 0; j < count; j++)
		{
			int from = bookItems[j].Move >> 8;
			int to = bookItems[j].Move & 0xff;
			printf("move:(row %d,col %d)->(row %d,col %d),score:%d  ", from>>4, from&0xf, to >> 4, to & 0xf, bookItems[j].Score);
		}
		printf("\n");
	}
	system("pause");
	return 0;
}