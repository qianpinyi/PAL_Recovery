#ifndef NTFS_CPP
#define NTFS_CPP

namespace PAL_FileSystem
{
	using Sint8=char;
	using Sint16=short;
	using Sint32=int;
	using Sint64=long long;
//	using Sint128=__int128_t;
	using Uint8=unsigned char; 
	using Uint16=unsigned short;
	using Uint32=unsigned int;
	using Uint64=unsigned long long;
//	using Uint128=__uint128_t;
	
	const unsigned SectorSize=512;
	
	//Becarefull that the struct is aligned to 4 or 8(64bit compiler) bytes!!
	
	struct Sector
	{
		Uint8 data[SectorSize];
		
		inline Uint8& operator [] (unsigned pos)
		{return data[pos];}
	}__attribute__((packed));
	
	struct GUID
	{
		Uint8 data[16];
		
		bool operator == (const GUID &tar) const
		{
			for (int i=0;i<16;++i)
				if (data[i]!=tar.data[i])
					return 0;
			return 1;
		}
		
		bool Empty() const
		{
			for (int i=0;i<16;++i)
				if (data[i]!=0)
					return 0;
			return 1;
		}
	}__attribute__((packed));
	
	struct GPTHeader
	{
		Uint8 Signature[8];
		Uint32 Revision;
		Uint32 HeaderBytes;
		Uint32 HeaderCRC;
		Uint32 Reserved;
		Uint64 CurrentLBA;
		Uint64 BackupLBA;
		Uint64 FirstUsableLBA;
		Uint64 LastUsableLBA;
		GUID DiskGUID;
		Uint64 EntriesStartingLBA;
		Uint32 EntriesCount;
		Uint32 EntryBytes;
		Uint32 EntriesCRC;
		Uint8 Padding[420];
		
		bool SignatureCorrect() const
		{return Signature[0]=='E'&&Signature[1]=='F'&&Signature[2]=='I'&&Signature[3]==' '
			  &&Signature[4]=='P'&&Signature[5]=='A'&&Signature[6]=='R'&&Signature[7]=='T';}
	}__attribute__((packed));
	
	struct GPTEntry
	{
		GUID PartitionType;
		GUID ID;
		Uint64 FirstLBA;
		Uint64 LastLBA;
		Uint64 Attribute;
		Uint8 PartitionName[72];
	};
	
	struct GPTEntrySector
	{
		GPTEntry entries[4];
		
		inline GPTEntry& operator [] (unsigned pos)
		{return entries[pos];}
	}__attribute__((packed));
	
	struct GPTEntries
	{
		GPTEntry entries[128];
		
		inline GPTEntry& operator [] (unsigned pos)
		{return entries[pos];}
	}__attribute__((packed));
	
	struct GPT
	{
		
	}__attribute__((packed));
	
	struct NTFS_DBR
	{
		Uint8 JMP52NOP[3];
		Uint8 Signature[8];
		
//		Uint8 BPB[73];
		Uint8 BytesPerSector[2];
		Uint8 SectorsPerCluster;
		Uint8 ReservedSectors[2];
		Uint8 Zero_1[3];
		Uint8 Unused_1[2];
		Uint8 MediaDescriptor;
		Uint8 Zero_2[2];
		Uint16 SectorsPerTrack;
		Uint16 HeadsCount;
		Uint32 HiddenSectors;
		Uint32 Unused_2;
		Uint32 Unused_3;//Should be 0x80008000
		Uint64 TotalSectors;
		Uint64 MFT_LCN;
		Uint64 MFTMirrLCN;
		Uint8 BytesPerFileRecord;//Const 1024Bytes
		Uint8 Unused_4[3];
		Uint8 ClusterPerIndexBuffer;
		Uint8 Unused_5[3];
		Uint8 VolumeSerialNumber[8];
		Uint32 CRC;

		Uint8 BootLoader[426];
		Uint16 EndTag;
		
		inline bool SignatureCorrect() const
		{return Signature[0]==0x4E&&Signature[1]==0x54&&Signature[2]==0x46&&Signature[3]==0x53&&
				Signature[4]==0x20&&Signature[5]==0x20&&Signature[6]==0x20&&Signature[7]==0x20;}
	}__attribute__((packed));
	
	struct MFT_Record
	{
		Sector data[2];
		
		inline Uint8& operator [] (unsigned pos)
		{return ((Uint8*)data)[pos];}
	}__attribute__((packed));
	
	struct MFT_Header
	{
		Uint8 SignatureFILE[4];
		Uint16 UpdateSequenceOffset;
		Uint16 UpdateSequenceSize;
		Uint64 LogfileSequenceNumber;
		Uint16 UseDeleteCount;
		Uint16 HardlinkCount;
		Uint16 FirstAttributeOffset;
		Uint16 TypeAndState;
		Uint32 RecordLogicalSize;
		Uint32 RecordPhysicalSize;
		Uint64 FileIndexNumber;
		Uint16 NextFreeID;
		Uint8 Unused[2];
		Uint32 MFTNumber;
		Uint16 UpdateSequence;
		Uint8 UpdateArray1[2];
		Uint8 UpdateArray2[2]; 
		Uint8 Padding[2];
		
		bool SignatureIsFILE() const
		{return SignatureFILE[0]=='F'&&SignatureFILE[1]=='I'&&SignatureFILE[2]=='L'&&SignatureFILE[3]=='E';}
	}__attribute__((packed));
	
	const Uint32 MFT_AttributeID_STANDARD_INFORMATION=0x10,
				 MFT_AttributeID_ATTRIBUTE_LIST=0x20,
				 MFT_AttributeID_FILE_NAME=0x30,
				 MFT_AttributeID_OBJECT_ID=0x40,
				 MFT_AttributeID_SECURITY_DESCRIPTOR=0x50,
				 MFT_AttributeID_VOLUME_NAME=0x60,
				 MFT_AttributeID_VOLUME_INFOMATION=0x70,
				 MFT_AttributeID_DATA=0x80,
				 MFT_AttributeID_INDEX_ROOT=0x90,
				 MFT_AttributeID_INDEX_ALLOCATION=0xA0,
				 MFT_AttributeID_BITMAP=0xB0,
				 MFT_AttributeID_REPARSE_POINT=0xC0,
				 MFT_AttributeID_EA_INFORMATION=0xD0,
				 MFT_AttributeID_EA=0xE0,
				 MFT_AttributeID_LOCGGED_UTILITY_STREAM=0x0100;
	
	struct MFT_AttributeDef
	{
		Uint8 AttributeName[128];
		Uint32 AttributeID;
		Uint32 DisplayRule;
		Uint32 ProofreadRule;
		Uint32 TypeFlag;//0x00:Unlimited 0x02:Index 0x40:Resident 0x80:Non-resident
		Uint64 MinAttributeBodyBytes;
		Uint64 MaxAttributeBodyBytes;
	}__attribute__((packed));
	
	struct MFT_AttributeOverall
	{
		Uint32 AttributeID;
		Uint32 TotalBytes;//Align to 8bytes
		Uint8 IsNonresident;
		Uint8 AttributeNameWords;//Word==Two bytes
		Uint16 AttributeNameOffset;
		Uint16 AttributeFlag;//0x0001:Compressed 0x4000:Encrypted 0x8000:Sparsed ...
		Uint16 IDInMFT;
	}__attribute__((packed));
	
	struct MFT_AttributeResident
	{
		Uint32 AttributeBodyBytes;//Not necessary align to 8bytes
		Uint16 AttributeBodyOffset;//Align to 8bytes
		Uint8 IndexedFlag;
		Uint8 Unused;
		//AttributeName[2*AttributeNameWords]
		//Align??
		//AttributeBody[AttributeBodyBytes]
		//Align
	}__attribute__((packed));
	
	struct MFT_AttributeNonresident
	{
		Uint64 StartVCN;
		Uint64 EndVCN;
		Uint16 DataRunOffset;
		Uint16 CompressUintBytes;//0 means no compress
		Uint8 Unused[4];
		Uint64 AttributePhysicalSize;
		Uint64 AttributeLogicalSize;//may not be correct
		Uint64 AttributeRawSize;
		//AttributeName[2*AttributeNameWords]
		//Align??
		//RunList
		//Align
	}__attribute__((packed));
	
	struct RunList
	{
		
	}__attribute__((packed));
	
	const Uint32 MFT_StandardFileAttribute_ReadOnly=0x0001,
				 MFT_StandardFileAttribute_Hidden=0x0002,
				 MFT_StandardFileAttribute_System=0x0004,
				 MFT_StandardFileAttribute_Archived=0x0020,
				 MFT_StandardFileAttribute_Device=0x0040,
				 MFT_StandardFileAttribute_Regular=0x0080,
				 MFT_StandardFileAttribute_Temporary=0x0100,
				 MFT_StandardFileAttribute_Sparse=0x0200,
				 MFT_StandardFileAttribute_Repare=0x0400,
				 MFT_StandardFileAttribute_Compressed=0x0800,
				 MFT_StandardFileAttribute_Offline=0x1000,
				 MFT_StandardFileAttribute_Unindexed=0x2000,
				 MFT_StandardFileAttribute_Encrypted=0x4000;
	
	struct MFT_AttributeBody_STANDARD_INFORMATION
	{
		Uint64 CreatedTime;
		Uint64 WriteTime;
		Uint64 MFTUpdateTime;
		Uint64 AccessTime;
		Uint32 AttributeFlags;
		Uint32 MaxVersion;
		Uint32 Version;
		Uint32 ClassifyID;
		Uint32 OwnerID;
		Uint32 SecurityID;
		Uint64 QuotaUsage;
		Uint64 UpdateSerialNumber;
	}__attribute__((packed));
	
	struct MFT_AttributeBody_FILE_NAME//other attributes in this struct is not correct sometimes
	{
		Uint64 ParentMFTIndex;
		Uint64 CreatedTime;
		Uint64 WriteTime;
		Uint64 MFTUpdateTime;
		Uint64 AccessTime;
		Uint64 FilePhysicalSize;
		Uint64 FileLogicalSize;
		Uint32 AttributeFlags;
		Uint32 EAsAndReparse;
		Uint8 FilenameWords;
		Uint8 FilenameNamespace;
		//FileName[FilenameWords*2]
		//Align
	}__attribute__((packed));
	
	struct MFT_IndexRoot
	{
		Uint32 IndexType;
		Uint32 CheckrRule;
		Uint32 IndexBlockSize;
		Uint8 ClustersPerIndexBlock;
		Uint8 Padding[3];
	}__attribute__((packed));

	struct MFT_IndexBlock
	{
		Uint8 IndexSignature[4];//INDX
		Uint16 UpdateSerialNumberOffset;
		Uint16 UpdateSerialWords;
		Uint64 LogFileSerialNumber;
		Uint64 IndexBlockNumber;
		//IndexHeader;
		//UpdateSerial[UpdateSerialWords*2];
	}__attribute__((packed));
	
	struct MFT_IndexHeader
	{
		Uint32 FirstIndexItemOffset;
		Uint32 IndexLogicalSize;
		Uint32 IndexPhysicalSize;
		Uint8 IndexItemType;
		Uint8 Padding[3];
		//MFT_IndexItem
	}__attribute__((packed));
	
	struct MFT_IndexItemHead
	{
		Uint64 MFTIndex;
		Uint16 IndexSize;
		Uint16 FilenameAttributeSize;
		Uint8 IndexFlag;
		Uint8 Padding[3];
		//MFT_AttributeBody_FILE_NAME
		//IndexBlockID(if is not leaf node);
		
		bool IsEndIndex() const
		{return MFTIndex==0&&IndexSize==0x18&&FilenameAttributeSize==0&&IndexFlag==3;}
		
		bool IsEndTag() const
		{return MFTIndex==0&&IndexSize==0x10&&FilenameAttributeSize==0&&IndexFlag==2;}
		
		bool IsFullZero() const
		{return MFTIndex==0&&IndexSize==0&&FilenameAttributeSize==0&&IndexFlag==0;}
	}__attribute__((packed));
};

#endif
