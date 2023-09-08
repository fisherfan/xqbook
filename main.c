#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"//BerkeleyDBͷ�ļ�
#pragma comment(lib, "legacy_stdio_definitions.lib")//����BerkeleyDB���ļ��ǵͰ汾VC����ģ�vc2010������Ҫ����������ļ�������Ӵ���
FILE* __cdecl __iob_func(unsigned i)//�߰汾vc��__iob_func������__acrt_iob_func�������Լ�����һ�½��BerkeleyDB���ļ��Ҳ���__iob_func������
{
	return __acrt_iob_func(i);
}
#pragma comment(lib, "ws2_32.lib")//BerkeleyDB���ļ����������
#pragma comment(lib, "libdb_small181s.lib")//BerkeleyDB���ļ�

typedef unsigned char bool;
#define true 1
#define false 0

typedef struct XQKEY
{
	unsigned char Key[128];
	int KeyLen;
	bool MirrorUD;//�Ƿ����¾�����
	bool MirrorLR;//�Ƿ����Ҿ�����
	int Rows;//�����ж�����
	int Cols;//�����ж�����
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

void FillData(unsigned char *data, int bits, int val, int size)//���ֽ����������size������λ����val�������ݵ�data��bits��ָ��λ�ã�
{
	int space = 8 - bits % 8;//��ǰλ��ʣ���λ������Χ��1~8��
	for (int sizeLeft = size, i = bits / 8; sizeLeft > 0; space = 8, i++)//ʹ�ô��ģʽ������ݣ����ֽ���ǰ���ֽ��ں�
	{
		data[i] &= 0xff << space;//ʣ��λ����
		unsigned char dat;//�����������
		int sizeFill;//�������λ��
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
		data[i] |= (0xff >> (8 - space)) & dat;//������ݣ�ע��dat��Ĳ����λ������������������Ҫ��0��
		sizeLeft -= sizeFill;
	}
}

int RestoreData(const unsigned char *data, int bits, int size)//���ֽ������л�ԭsize������λ����data��bits��ָλ�ø������ݲ����أ�
{
	int val = 0;
	int space = 8 - bits % 8;//��ǰλ��ʣ���λ������Χ��1~8��
	for (int sizeLeft = size, i = bits / 8; sizeLeft > 0; space = 8, i++)
	{
		unsigned char dat;//���λ�ԭ����
		int sizeRestore;//���λ�ԭλ��
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
		val <<= sizeRestore;//�����ڳ�λ��
		val |= (0xff >> (8 - sizeRestore)) & dat;//��ԭ���ݣ�ע��dat��Ĳ����λ������������������Ҫ��0��
		sizeLeft -= sizeRestore;
	}
	//���ȡ��������ֵ��1�ֽڻ�2�ֽڣ���ôҪ����һ��ת������������ֵ��ȡ������
	if (size == 8)
		val = (char)val;
	else if (size == 16)
		val = (short)val;

	return val;
}

void FenToKey(const char *fen, XQKEY *xqKey)
{
	//fenת������
	int turn = *(strchr(fen, ' ') + 1) != 'b';//����˳��0��1��
	int size = GetRowsAndCols(fen, &xqKey->Rows, &xqKey->Cols);
	char *ary = malloc(size);
	memset(ary, -1, size);//��-1��ʾ�ո��ӣ���Ϊ0������ʾ������
	for (int i = 0, index = 0; fen[i] != ' ' && index < size; i++)
	{
		if (fen[i] >= '0' && fen[i] <= '9')
		{
			index += fen[i] - '0';
		}
		else if(fen[i]!='/')
		{
			char val = -1;
			switch (turn == 0 ? (fen[i] ^ 0x20) : fen[i])//ע������������˳���Ǻڷ��Ļ�ת����Сд������д��Զ��ʾ���巽��Сд��ʾ�����巽��
			{
			case 'X':
			case 'x':val = 0; break;//˫��������ͬһ���룬��Ϊ��һ��ת���˴�Сд�������岻�����ְ�����ɫ������ɷֿɲ��֣�����������б������ְ�����ɫ������������ͬһ����
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
	//���¾��������Ҫ�Ļ���
	xqKey->MirrorUD = false;
	if (turn == 0)//���巽�Ǻڷ��Ļ���Ҫ���¾���
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
	//���Ҿ��������Ҫ�Ļ���
	xqKey->MirrorLR = false;
	for (int row = 0; row < xqKey->Rows && !xqKey->MirrorLR; row++)
	{
		for (int col = 0; col < xqKey->Cols / 2 && !xqKey->MirrorLR; col++)
		{
			int index = row * xqKey->Cols + col;
			int index2 = row * xqKey->Cols + (xqKey->Cols - 1 - col);
			if (ary[index2] > ary[index])//�ұ߱���ߴ�Ļ���Ҫ���Ҿ���
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
	//����key
	//����һ��ֱ���׶�
	//const int codeBits = 4;//���ӱ���λ�����̶�ֵ��
	//int bits = 0;//�������λ��
	//for (int index = 0; index < size; index++)
	//{
	//	if (ary[index] == -1)//����ֱ����0��ռ1λ��
	//	{
	//		FillData(xqKey->Key, bits, 0, 1);
	//		bits++;
	//	}
	//	else//������1+���ӱ��루ռ1+4=5λ��
	//	{
	//		FillData(xqKey->Key, bits, (1 << 4) | ary[index], 1 + codeBits);
	//		bits += 1 + codeBits;
	//	}
	//}
	//xqKey->KeyLen = (bits + 7) / 8;
	//���������ٶȿ�Щ
	xqKey->KeyLen = 0;
	unsigned int buffer = 0;//������
	const int bufferBits = sizeof(buffer) * 8;//��������һ���ж���λ���̶�ֵ��
	const int codeBits = 4;//���ӱ���λ�����̶�ֵ��
	int bits = 0;//���������ѱ����λ��
	for (int index = 0; index < size; index++)
	{
		if (ary[index] == -1)//����ֱ����0��ռ1λ��
		{
			bits++;
		}
		else//������1+���ӱ��루ռ1+4=5λ��
		{
			buffer |= 1 << (bufferBits - bits - 1);
			buffer |= ary[index] << (bufferBits - bits - 1 - codeBits);
			bits += 1 + codeBits;
		}
		if (bits >= bufferBits / 2)//������������ݴﵽһ����д��
		{
			for (int i = 8; i <= bufferBits / 2; i += 8)
				xqKey->Key[xqKey->KeyLen++] = buffer >> (bufferBits - i);
			buffer <<= bufferBits / 2;
			bits -= bufferBits / 2;
		}
	}
	if (bits > 0)//�ѻ�������ʣ�������д��
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
	int bits = 0;//������λ��
	//�ŷ�
	FillData(data, bits,
		MirrorMove(bookItem->Move, xqKey->MirrorUD, xqKey->MirrorLR, xqKey->Rows, xqKey->Cols),//���ݾ����Ƿ��񣬾����ŷ��Ƿ�Ҳ���񱣴�
		16);
	bits += 16;
	//����
	mask = GetNumberMask(bookItem->Score);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Score, size);
	bits += size;
	//ʤ
	mask = GetNumberMask(bookItem->Win);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Win, size);
	bits += size;
	//��
	mask = GetNumberMask(bookItem->Draw);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Draw, size);
	bits += size;
	//��
	mask = GetNumberMask(bookItem->Lost);
	FillData(data, bits, mask, 2);
	bits += 2;
	size = (mask == 3 ? 4 : mask) * 8;
	FillData(data, bits, bookItem->Lost, size);
	bits += size;
	//��Ч
	FillData(data, bits, bookItem->Valid, 1);
	bits += 1;
	//��ע
	int dataLen = (bits + 7) / 8;
	int memoLen = strlen(bookItem->Memo);
	memcpy(data + dataLen, bookItem->Memo, memoLen + 1);//ע��memo�������Ҫ��֤��utf8�����ٲ���

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
		if (dbp->get(dbp, NULL, &key, &data, 0) == 0)//����þ����Ѿ����ŷ����µ��ŷ�׷���������
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
		//�ŷ�
		bookItems[count].Move = MirrorMove(RestoreData(data, bits, 16), xqKey->MirrorUD, xqKey->MirrorLR, xqKey->Rows, xqKey->Cols);//���ﱣ����ŷ������Ǿ����
		bits += 16;
		//����
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Score = RestoreData(data, bits, size);
		bits += size;
		//ʤ
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Win = RestoreData(data, bits, size);
		bits += size;
		//��
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Draw = RestoreData(data, bits, size);
		bits += size;
		//��
		mask = RestoreData(data, bits, 2);
		bits += 2;
		size = (mask == 3 ? 4 : mask) * 8;
		bookItems[count].Lost = RestoreData(data, bits, size);
		bits += size;
		//��Ч
		bookItems[count].Valid = RestoreData(data, bits, 1);
		bits += 1;
		//��ע
		int dataLen = (bits + 7) / 8;
		int memoLen = strlen(data + dataLen);
		bits = (dataLen + memoLen + 1) * 8;
		if (sizeof(bookItems[count].Memo) - 1 < memoLen)
			memoLen = sizeof(bookItems[count].Memo) - 1;
		memcpy(bookItems[count].Memo, data + dataLen, memoLen);
		bookItems[count].Memo[memoLen] = 0;//ע��memo��������utf8����
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

	//�����������
	//XQKEY xqKey;
	//FenToKey("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1", &xqKey);//��ʼ����
	//BOOKITEM bookItem = { 0 };
	//bookItem.Move = 0x6656;//������һ��6��6��->5��6�У�
	//bookItem.Score = 1;
	//bookItem.Win = 128;
	//bookItem.Draw = 65536;
	//bookItem.Lost = -1;
	//bookItem.Valid = 1;
	//BookInsert(bookName, &xqKey, &bookItem);

	//�����������
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

	//��ѯ����
	const char* fens[] = 
	{ 
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1",//��ʼ����
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C2C4/9/RNBAKABNR b - - 0 1",//�ڶ�ƽ�����
		"rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/4C2C1/9/RNBAKABNR b - - 0 1",//�ڰ�ƽ����棨�������Ҿ���
		"rnbakabnr/9/1c2c4/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1",//���ȣ��ڣ�ƽ�����棨�������¾���
		"rnbakabnr/9/4c2c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1" //���ȣ��ڣ�ƽ�����棨��������+���Ҿ���
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