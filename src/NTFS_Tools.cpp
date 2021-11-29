#ifndef NTFS_TOOLS_CPP
#define NTFS_TOOLS_CPP 1

#include <windows.h>
#include <PAL_BasicFunctions/PAL_BasicFunctions_0.cpp>
#include <PAL_DataStructure/PAL_Tuple.cpp>
#include "NTFS.cpp"

#ifndef NTFS_ReadDiskBufferSectorSize
#define NTFS_ReadDiskBufferSectorSize 1
#endif

namespace PAL_FileSystem
{
	int ReadSector(HANDLE h,unsigned long long LBA,Sector sec[],int cnt=1)
	{
		DWORD len=0;
		LARGE_INTEGER li;
		li.QuadPart=LBA<<9;
		if (LBA!=(unsigned long long)-1&&!SetFilePointerEx(h,li,NULL,0))
			return 0;
		if (!ReadFile(h,sec,SectorSize*cnt,&len,NULL)||len<SectorSize*cnt)
			return len/SectorSize;
//		for (int i=0;i<cnt;++i)
//			if (!ReadFile(h,sec[i].data,512,&len,NULL)||len!=512)
//				return i;
		return cnt;
	}
	
	int ReadDisk(HANDLE h,unsigned long long pos,unsigned char *dst,unsigned long long len)
	{
		static HANDLE lastH=NULL;
		static Uint8 buffer[NTFS_ReadDiskBufferSectorSize*SectorSize];
		static unsigned long long bufferPos=0;
		
		unsigned long long p=0;
		while (len)
		{
			if (lastH==NULL||lastH!=h||!InRange(pos,bufferPos,bufferPos+NTFS_ReadDiskBufferSectorSize*SectorSize-1))
			{
				if (ReadSector(lastH=h,pos>>9,(Sector*)buffer,NTFS_ReadDiskBufferSectorSize)==0)
					return p;
				bufferPos=pos>>9<<9;
			}
			while (len&&InRange(pos,bufferPos,bufferPos+NTFS_ReadDiskBufferSectorSize*SectorSize-1))
				dst[p]=buffer[pos-bufferPos],++p,++pos,--len;
		}
		return p;
	}
	
	int ReadDiskBuffer(Uint8 *src,Uint64 srcPos,Uint64 srcLen,Uint64 pos,unsigned char *dst,Uint64 len)
	{
		unsigned long long p=0;
		while (len&&InRange(pos,srcPos,srcPos+srcLen-1))
		{
			dst[p]=src[pos-srcPos+p];
			++p;--len;
		}
		return p;
	}
	
	int ReadMFT(HANDLE h,unsigned long long pos,MFT_Record &mft)
	{
		if (ReadDisk(h,pos,(Uint8*)&mft,sizeof(MFT_Record))!=sizeof(MFT_Record))
			return 1;
		return 0;
	}
	
	int ReadMFTAndUpdate(HANDLE h,unsigned long long pos,MFT_Record &mft)
	{
		int re=0;
		if (re=ReadMFT(h,pos,mft))
			return re;
		mft[0x1fe]=mft[0x32];
		mft[0x1ff]=mft[0x33];
		mft[0x3fe]=mft[0x34];
		mft[0x3ff]=mft[0x35];
		return 0;
	}
	
	int ReadOneMFTAttribute(HANDLE h,MFT_AttributeOverall &tar,Uint64 startPos,Uint64 *endPos=NULL)
	{
		if (ReadDisk(h,startPos,(Uint8*)&tar,sizeof(MFT_AttributeOverall))!=sizeof(MFT_AttributeOverall))
			return 1;
		if (endPos!=NULL)
			*endPos=startPos+tar.TotalBytes;
		return 0;
	}
	
	int ReadOneMFTAttribute(Uint8 *src,Uint64 srcPos,Uint64 srcLen,MFT_AttributeOverall &tar,Uint64 startPos,Uint64 *endPos=NULL)
	{
		if (ReadDiskBuffer(src,srcPos,srcLen,startPos,(Uint8*)&tar,sizeof(MFT_AttributeOverall))!=sizeof(MFT_AttributeOverall))
			return 1;
		if (endPos!=NULL)
			*endPos=startPos+tar.TotalBytes;
		return 0;
	}
	
	PAL_DS::Triplet <unsigned,unsigned long long,long long> GetRunList(HANDLE h,unsigned long long pos)
	{
		Uint8 runlist[32];
		if (ReadDisk(h,pos,runlist,1)!=1)
			return {(unsigned)-1,0,0};
		if (runlist[0]==0)
			return {0,0,0};
		Uint8 s=runlist[0]>>4,l=runlist[0]&0x0f;
		if (ReadDisk(h,pos+1,runlist+1,s+l)!=s+l)
			return {(unsigned)-1,0,0};
		unsigned long long Len=0;
		long long LCN=0;
		for (int i=0;i<l;++i)
			Len|=(Uint64)runlist[i+1]<<i*8;
		for (int i=0;i<s;++i)
			LCN|=(Uint64)runlist[l+1+i]<<i*8;
		if (runlist[l+s]>=128)
			for (int i=s;i<8;++i)
				LCN|=(Uint64)0xff<<i*8;
		return {s+l+1u,Len,LCN};
	}
	
	PAL_DS::Triplet <unsigned,unsigned long long,long long> GetRunList(Uint8 *src,Uint64 srcPos,Uint64 srcLen,Uint64 pos)
	{
		Uint8 runlist[32];
		if (ReadDiskBuffer(src,srcPos,srcLen,pos,runlist,1)!=1)
			return {(unsigned)-1,0,0};
		if (runlist[0]==0)
			return {0,0,0};
		Uint8 s=runlist[0]>>4,l=runlist[0]&0x0f;
		if (ReadDiskBuffer(src,srcPos,srcLen,pos+1,runlist+1,s+l)!=s+l)
			return {(unsigned)-1,0,0};
		unsigned long long Len=0;
		long long LCN=0;
		for (int i=0;i<l;++i)
			Len|=(Uint64)runlist[i+1]<<i*8;
		for (int i=0;i<s;++i)
			LCN|=(Uint64)runlist[l+1+i]<<i*8;
		if (runlist[l+s]>=128)
			for (int i=s;i<8;++i)
				LCN|=(Uint64)0xff<<i*8;
		return {s+l+1u,Len,LCN};
	}
};

#endif
