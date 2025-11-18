#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"//BerkeleyDB头文件
#pragma comment(lib, "legacy_stdio_definitions.lib")//由于BerkeleyDB库文件是低版本VC编译的（vc2010），需要引入这个库文件解决链接错误
FILE* __cdecl __iob_func(unsigned i)//高版本vc将__iob_func改名成__acrt_iob_func，这里自己定义一下解决BerkeleyDB库文件找不到__iob_func的问题
{
	return __acrt_iob_func(i);
}
#pragma comment(lib, "ws2_32.lib")//BerkeleyDB库文件依赖这个库
#pragma comment(lib, "libdb_small181s.lib")//BerkeleyDB库文件

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

void FillData(unsigned char *data, int bits, int val, int size)//向字节数组中填充size个比特位（从val复制数据到data中bits所指的位置）
{
	int space = 8 - bits % 8;//当前位置剩余的位数（范围：1~8）
	for (int sizeLeft = size, i = bits / 8; sizeLeft > 0; space = 8, i++)//使用大端模式填充数据（高字节在前低字节在后）
	{
		data[i] &= 0xff << space;//剩余位清零
		unsigned char dat;//本次填充数据
		int sizeFill;//本次填充位数
		if (sizeLeft < space)
		{
			dat = val << (space - sizeLeft);
			sizeFill = sizeLeft;
		}
		else
		{
			dat = val >> (sizeLeft - space);
			sizeFill = space;
		}
		data[i] |= (0xff >> (8 - space)) & dat;//填充数据（注意dat里的不相关位可能有垃圾数据所以要置0）
		sizeLeft -= sizeFill;
	}
}

int RestoreData(const unsigned char *data, int bits, int size)//从字节数组中还原size个比特位（从data中bits所指位置复制数据并返回）
{
	int val = 0;
	int space = 8 - bits % 8;//当前位置剩余的位数（范围：1~8）
	for (int sizeLeft = size, i = bits / 8; sizeLeft > 0; space = 8, i++)
	{
		unsigned char dat;//本次还原数据
		int sizeRestore;//本次还原位数
		if (sizeLeft < space)
		{
			dat = data[i] >> (space - sizeLeft);
			sizeRestore = sizeLeft;
		}
		else
		{
			dat = data[i];
			sizeRestore = space;
		}
		val <<= sizeRestore;//左移腾出位置
		val |= (0xff >> (8 - sizeRestore)) & dat;//还原数据（注意dat里的不相关位可能有垃圾数据所以要置0）
		sizeLeft -= sizeRestore;
	}
	//如果取出来的数值是1字节或2字节，那么要进行一次转换处理，否则负数值会取成正数
	if (size == 8)
		val = (char)val;
	else if (size == 16)
		val = (short)val;

	return val;
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
	//上下镜像（如果需要的话），注意这里实际是旋转局面，否则开局局面炮二平五会对应炮８平５，虽然不影响功能但也有点别扭
	xqKey->MirrorUD = false;
	if (turn == 0)//走棋方是黑方的话需要上下镜像
	{
		for (int row = 0; row < xqKey->Rows / 2; row++)
		{
			for (int col = 0; col < xqKey->Cols; col++)
			{
				int index = row * xqKey->Cols + col;
				int index2 = (xqKey->Rows - 1 - row) * xqKey->Cols + (xqKey->Cols - 1 - col);
				char tmp = ary[index];
				ary[index] = ary[index2];
				ary[index2] = tmp;
			}
		}
		xqKey->MirrorUD = true;
	}
	//左右镜像（如果需要的话）
	xqKey->MirrorLR = false;
	bool lrDone = false;
	for (int row = 0; row < xqKey->Rows && !lrDone; row++)
	{
		for (int col = 0; col < xqKey->Cols / 2 && !lrDone; col++)
		{
			int index = row * xqKey->Cols + col;
			int index2 = row * xqKey->Cols + (xqKey->Cols - 1 - col);
			if (ary[index] != ary[index2])//两边出现不一样棋子了要决定是否左右镜像
			{
				xqKey->MirrorLR = ary[index2] > ary[index];//右边比左边大的话需要左右镜像
				lrDone = true;
			}
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
	//方法一：直观易懂
	//const int codeBits = 4;//棋子编码位数（固定值）
	//int bits = 0;//已填入的位数
	//for (int index = 0; index < size; index++)
	//{
	//	if (ary[index] == -1)//无棋直接用0（占1位）
	//	{
	//		FillData(xqKey->Key, bits, 0, 1);
	//		bits++;
	//	}
	//	else//有棋用1+棋子编码（占1+4=5位）
	//	{
	//		FillData(xqKey->Key, bits, (1 << 4) | ary[index], 1 + codeBits);
	//		bits += 1 + codeBits;
	//	}
	//}
	//xqKey->KeyLen = (bits + 7) / 8;
	//方法二：速度快些
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
		if (index == size - 1 || bufferBits - bits < (ary[index + 1] == -1 ? 1 : codeBits + 1))//最后一个格子已读取或缓冲区剩余空间不足以存放下一个格子则写入
		{
			int threashold = index == size - 1 ? 1 : 8;//满足写入条件的阈值（没到最后一个格子时要把不足8位的数据保留，到最后一个格子时要全部写入）
			while (bits >= threashold)
			{
				xqKey->Key[xqKey->KeyLen++] = buffer >> (bufferBits - 8);
				buffer <<= 8;
				bits -= 8;
			}
		}
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
			//由于FenToKey函数中上下镜像实际是旋转局面，所以这里也要把左右镜像
			fromCol = cols - 1 - fromCol;
			toCol = cols - 1 - toCol;
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

unsigned char GetNumberMask(int num)
{
	if (num == 0)
		return 0;
	else if (num >= -128 && num < 127)
		return 1;
	else if (num >= -32768 && num < 32767)
		return 2;
	else
		return 3;
}

int BookItemToData(const BOOKITEM *bookItem, const XQKEY *xqKey, unsigned char *data)
{
	int mask;
	int size;
	int bits = 0;//已填充的位数
	//着法
	FillData(data, bits,
		MirrorMove(bookItem->Move, xqKey->MirrorUD, xqKey->MirrorLR, xqKey->Rows, xqKey->Cols),//根据局面是否镜像，决定着法是否也镜像保存
		16);
	bits += 16;
	//分数
	mask = GetNumberMask(bookItem->Score);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Score, size);
	bits += size;
	//胜
	mask = GetNumberMask(bookItem->Win);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Win, size);
	bits += size;
	//和
	mask = GetNumberMask(bookItem->Draw);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Draw, size);
	bits += size;
	//负
	mask = GetNumberMask(bookItem->Lost);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Lost, size);
	bits += size;
	//有效
	FillData(data, bits, bookItem->Valid, 1);
	bits += 1;
	//备注
	int dataLen = (bits + 7) / 8;
	int memoLen = strlen(bookItem->Memo);
	memcpy(data + dataLen, bookItem->Memo, memoLen + 1);//注意memo里的内容要保证是utf8编码再插入

	return dataLen + memoLen + 1;
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
		key.data = (void*)xqKey->Key;
		key.size = xqKey->KeyLen;
		unsigned char *pData = malloc(4096);
		if (dbp->get(dbp, NULL, &key, &data, 0) == 0)//如果该局面已经有着法，新的着法追加在其后面
			memcpy(pData, data.data, data.size);
		data.size += BookItemToData(bookItem, xqKey, pData + data.size);
		data.data = pData;
		dbp->put(dbp, NULL, &key, &data, 0);
		free(pData);
		dbp->close(dbp, 0);
	}
	return success;
}

int DataToBookItems(const unsigned char *data, int dataLen, const XQKEY *xqKey, BOOKITEM *bookItems)
{
	int count = 0;
	int bits = 0;
	int mask;
	int size;
	for (; bits / 8 < dataLen; count++)
	{
		//着法
		bookItems[count].Move = MirrorMove(RestoreData(data, bits, 16), xqKey->MirrorUD, xqKey->MirrorLR, xqKey->Rows, xqKey->Cols);//库里保存的着法可能是镜像的
		bits += 16;
		//分数
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Score = RestoreData(data, bits, size);
		bits += size;
		//胜
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Win = RestoreData(data, bits, size);
		bits += size;
		//和
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Draw = RestoreData(data, bits, size);
		bits += size;
		//负
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Lost = RestoreData(data, bits, size);
		bits += size;
		//有效
		bookItems[count].Valid = RestoreData(data, bits, 1);
		bits += 1;
		//备注
		int dataLen = (bits + 7) / 8;
		int memoLen = strlen(data + dataLen);
		bits = (dataLen + memoLen + 1) * 8;
		if (sizeof(bookItems[count].Memo) - 1 < memoLen)
			memoLen = sizeof(bookItems[count].Memo) - 1;
		memcpy(bookItems[count].Memo, data + dataLen, memoLen);
		bookItems[count].Memo[memoLen] = 0;//注意memo读出来是utf8编码
	}
	return count;
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
		key.data = (void*)xqKey->Key;
		key.size = xqKey->KeyLen;
		if (dbp->get(dbp, NULL, &key, &data, 0) == 0)
			count = DataToBookItems(data.data, data.size, xqKey, bookItems);
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
	//bookItem.Score = 1;
	//bookItem.Win = 128;
	//bookItem.Draw = 65536;
	//bookItem.Lost = -1;
	//bookItem.Valid = 1;
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
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR b - - 0 1",//黑先，初始局面（测试上下镜像）
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