/*
	PAL_Revovery 1.0
	By:qianpinyi
	21.11.25-29
*/
#define NTFS_ReadDiskBufferSectorSize 8

#include <PAL_GUI/PAL_GUI_0.cpp>
#include <PAL_BasicFunctions/PAL_System.cpp>
#include <PAL_DataStructure/PAL_SegmentTree.cpp>
#include "NTFS_Tools.cpp"
using namespace PAL_GUI;
using namespace Charset;
using namespace PAL_FileSystem;
const string ProgramName="PAL_Recovery",
			 ProgramVersion="1.1",
			 ProgramVersionDate="21.11.28";
string ProgramPath;

struct FileNode
{
	unsigned long long MFTIndex=0,
					   parentIndex=0,
					   mftPos=0,
					   FileSize=0;
	bool Deleted=0,IsDir=0;
	wchar_t *name=NULL;
	set <unsigned long long> childs;
	vector <Doublet<unsigned long long,long long> > dataruns;
	Uint8 *data=NULL;
	
	int Solved=0;
	FileNode *fa=NULL;
	unsigned long long DeletedSize=0,
					   OccupiedCnt=0;
	unsigned int HaveDeleted=0,
				 TreeSize=0,
				 BrokenCnt=0;
	
	~FileNode()
	{
		if (name)
			DELETEtoNULL(name);
		if (data)
			DeleteToNULL(data);
	}
};
map <unsigned long long,FileNode*> AllFiles;
PAL_SparseSegmentTree *DataBitmap=NULL;//InCluster(LCN)
atomic_bool Recoverying(0);

struct RecPath
{
	int type=0;//0:MainPage 1:disk 2:partition 3:MFT Dir 4:MFT File
	string name;
	
	unsigned long long DiskID=0;
	unsigned long long partitionLBA=0;
	FileNode *file=NULL;
};
vector <RecPath> PathHistory,PopHistory;
unsigned long long CurrentDiskID=-1,CurrentPartitionLBA=-1;
RecPath CurrentPath;
HANDLE CurrentDrive=0;
int FileSortMode=0;//0:MFTIndex 1:FileName 2:FileSize 3:Deleted FileSize
bool SepareteDirFile=0;
bool ReverseSort=0;

TwinLayerWithDivideLine *TwinLay=NULL;
SimpleBlockView <RecPath> *SBV=NULL;
ButtonI *Bu_Title=NULL,
		*Bu_Pre=NULL,
		*Bu_Nxt=NULL;
TinyText *TT_RecoveryState=NULL;
DropDownButtonI *DDB_Sort=NULL;
ProgressBar *ProBar=NULL;
SimpleTextBox *STB_Info=NULL;
using SBVData=SimpleBlockView <RecPath>::BlockViewData;

void SendInfoMsg(int type,const string &text,int ring=SetSystemBeep_None)//type 4:MsgBox 5:SystemMsg
{
	if (type==5) DD["SystemMsg"]<<text<<endl;
	else if (type==4) DD["MsgBox"]<<text<<endl;
	else DD[type]<<text<<endl;
	string typestr;
	RGBA co;
	switch (type)
	{
		case 0: typestr=PUIT("日志");		co=RGBA_BLUE_8[4];	break;
		case 1:	typestr=PUIT("警告");		co={255,89,0,255};	break;
		case 2:	typestr=PUIT("错误");		co=RGBA_RED;		break;
		case 3:	typestr=PUIT("调试");		co={11,116,27,255};	break;
		case 4:	typestr=PUIT("提示框");		co=ThemeColorMT[0];	break;
		case 5:	typestr=PUIT("系统提示");	co=RGBA_RED;		break;
	}
	STB_Info->AppendNewLine("["+typestr+"] "+text,co);
	SetSystemBeep(ring);
}

void ThreadSendInfoMsg(int type,const string &text,int ring=SetSystemBeep_None)
{
	PUI_SendFunctionEvent<Triplet<int,string,int> >([](Triplet<int,string,int> &data,int)->int
	{
		SendInfoMsg(data.a,data.b,data.c);
		return 0;
	},{type,text,ring});
}

void ShowMsgBox(const string &str,const string &title=PUIT("错误"),int ring=SetSystemBeep_Error)
{
	SendInfoMsg(4,title+":"+str,ring);
	(new MessageBoxButtonI(0,title,str))->AddButton(PUIT("确定"));
}

void ThreadShowMsgBox(const string &str,const string &title=PUIT("错误"),int ring=SetSystemBeep_Error)
{
	PUI_SendFunctionEvent<Triplet<string,string,int> >([](Triplet<string,string,int> &data,int)->int
	{
		ShowMsgBox(data.a,data.b,data.c);
		return 0;
	},{str,title,ring});
}

int GetMFTRecord(const unsigned long long mftpos)
{
	HANDLE h=CurrentDrive;
	MFT_Record mft;
	if (ReadMFTAndUpdate(h,mftpos,mft))
		return 1;
	MFT_Header &header=*(MFT_Header*)&mft;
	if (!header.SignatureIsFILE())
		return -1;
	FileNode *file=new FileNode();
	file->MFTIndex=header.MFTNumber;
	file->mftPos=mftpos;
	file->Deleted=!(header.TypeAndState&1);
	file->IsDir=header.TypeAndState&2;
	unsigned long long pos=mftpos+header.FirstAttributeOffset,tmp;
	int cnt80H=0;
	while (1)
	{
		MFT_AttributeOverall a;
		if (ReadOneMFTAttribute((Uint8*)&mft,mftpos,sizeof(MFT_Record),a,tmp=pos,&pos))
			return DeleteToNULL(file),1;
		if (a.AttributeID==0xffffffff||a.TotalBytes+tmp>=mftpos+1024)
			break;
		switch (a.AttributeID)
		{
			case 0x10:	break;
			case 0x30:
			{
				if (a.IsNonresident) 
				{
					SendInfoMsg(2,"MFTRecord attribute 30H of "+ullTOpadic(header.MFTNumber)+"H is not resident!",SetSystemBeep_Error);
					DeleteToNULL(file);
					return 2;
				}
				MFT_AttributeResident resi;
				if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+sizeof(MFT_AttributeOverall),(Uint8*)&resi,sizeof(resi))!=sizeof(resi))
					return DeleteToNULL(file),1;
				MFT_AttributeBody_FILE_NAME name;
				if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+=resi.AttributeBodyOffset,(Uint8*)&name,sizeof(name))!=sizeof(name))
					return DeleteToNULL(file),1;
				if (name.FilenameNamespace==2)
					break;
				file->parentIndex=name.ParentMFTIndex&0x0000ffffffffffff;
				file->name=new wchar_t[name.FilenameWords+1];
				memset(file->name,0,(name.FilenameWords+1)*sizeof(wchar_t));
				if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+=sizeof(name),(Uint8*)file->name,name.FilenameWords*2)!=name.FilenameWords*2)
					return DeleteToNULL(file),1;
				break;
			}
			case 0x80:
				++cnt80H;
				if (cnt80H>=2)
					if (a.AttributeNameWords>0)
						break;
				file->dataruns.clear();
				if (file->data)
					DELETEtoNULL(file->data);
				if (a.IsNonresident)
				{
					MFT_AttributeNonresident nonre;
					if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+sizeof(MFT_AttributeOverall),(Uint8*)&nonre,sizeof(nonre))!=sizeof(nonre))
						return DeleteToNULL(file),1;
					file->FileSize=nonre.AttributeLogicalSize;
					unsigned long long runlistPos=tmp+nonre.DataRunOffset,sumLCNpos=0;
					while (1)
					{
						auto tri=GetRunList((Uint8*)&mft,mftpos,sizeof(MFT_Record),runlistPos);
						if (tri.a==0)
							break;
						if (tri.a==-1)
							return DeleteToNULL(file),1;
						sumLCNpos+=tri.c;
						DataBitmap->AddSegment(sumLCNpos+1,sumLCNpos+tri.b,1);
						file->dataruns.push_back({tri.b,tri.c});
						runlistPos+=tri.a;
					}
				}
				else
				{
					MFT_AttributeResident resi;
					if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+sizeof(MFT_AttributeOverall),(Uint8*)&resi,sizeof(resi))!=sizeof(resi))
						return DeleteToNULL(file),1;
					file->FileSize=resi.AttributeBodyBytes;
					file->data=new Uint8[resi.AttributeBodyBytes];
					if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+resi.AttributeBodyOffset,(Uint8*)file->data,resi.AttributeBodyBytes)!=resi.AttributeBodyBytes)
						return DeleteToNULL(file),1;
				}
				break;
		}
	}
//	if (cnt80H>=2)
//		DD[2]<<"File "<<file->MFTIndex<<" "<<UnicodeToUtf8(file->name)<<" have multi data"<<endl;
	if (AllFiles.find(file->MFTIndex)==AllFiles.end())
		AllFiles[file->MFTIndex]=file;
	else
	{
		SendInfoMsg(1,"MFT Index conflict in "+llTOstr(file->MFTIndex)+" 0x"+ullTOpadic(mftpos)+"H",SetSystemBeep_Error);
		DeleteToNULL(file);
	}
	return 0;
}

void DFSCompleteInfo(int mftindex,FileNode *_fa)
{
	FileNode *file=AllFiles[mftindex];
	file->fa=_fa;
	file->HaveDeleted=file->Deleted;
	file->TreeSize=1;
	if (file->Deleted)
	{
		file->DeletedSize=file->FileSize;
		unsigned long long sumLCNpos=0,sumLen=0;
		for (auto [Len,LCN]:file->dataruns)
			sumLCNpos+=LCN,
			file->OccupiedCnt+=DataBitmap->QuerySegment(sumLCNpos+1,sumLCNpos+Len),
			sumLen+=Len;
		file->OccupiedCnt-=sumLen;
		if (file->OccupiedCnt!=0)
			file->BrokenCnt=1;
	}
		
	for (auto sp:file->childs)
		DFSCompleteInfo(sp,file);
		
	_fa->FileSize+=file->FileSize;
	_fa->DeletedSize+=file->DeletedSize;
	_fa->HaveDeleted+=file->HaveDeleted;
	_fa->TreeSize+=file->TreeSize;
	_fa->BrokenCnt+=file->BrokenCnt;
}

int GetCurrentPartitionAllFile()
{
	unsigned long long partitionStartLBA=CurrentPartitionLBA;
	HANDLE h=CurrentDrive;
	NTFS_DBR dbr;
	if (ReadSector(h,partitionStartLBA,(Sector*)&dbr)!=1)
		return 1;
	if (!dbr.SignatureCorrect())
		return 3;
	unsigned long long mftpos=partitionStartLBA+dbr.MFT_LCN*8<<9;
	MFT_Record mft;
	if (ReadMFTAndUpdate(h,mftpos,mft))
		return 1;
	MFT_Header &header=*(MFT_Header*)&mft;
	if (!header.SignatureIsFILE())
		return 1;
	unsigned long long pos=mftpos+header.FirstAttributeOffset,tmp;
	DataBitmap=new PAL_SparseSegmentTree((dbr.TotalSectors+7>>3)+1);
	while (1)
	{
		MFT_AttributeOverall a;
		if (ReadOneMFTAttribute((Uint8*)&mft,mftpos,sizeof(MFT_Record),a,tmp=pos,&pos))
			return 1;
		if (a.AttributeID==0xffffffff||a.AttributeID==0||a.TotalBytes+tmp>=mftpos+1024)
			break;
		if (a.AttributeID==0x80)
		{
			if (!a.IsNonresident)
			{
				SendInfoMsg(2,"$MFT attribute 80H of "+ullTOpadic(header.MFTNumber)+"H is not nonresident!",SetSystemBeep_Error);
				return 2;
			}
			MFT_AttributeNonresident nonre;
			if (ReadDiskBuffer((Uint8*)&mft,mftpos,sizeof(MFT_Record),tmp+sizeof(MFT_AttributeOverall),(Uint8*)&nonre,sizeof(nonre))!=sizeof(nonre))
				return 1;
			unsigned long long runlistPos=tmp+nonre.DataRunOffset,BasePos=partitionStartLBA<<9,sumLCNpos=0;
			while (1)
			{
				auto tri=GetRunList((Uint8*)&mft,mftpos,sizeof(MFT_Record),runlistPos);
				if (tri.a==0)
					break;
				if (tri.a==-1)
					return 1;
				sumLCNpos+=tri.c;
				DataBitmap->AddSegment(sumLCNpos+1,sumLCNpos+tri.b,1);
				for (unsigned long long i=0;i<tri.b*4;++i)
					if (GetMFTRecord(BasePos+tri.c*4096+i*1024)==1)
						return 1;
				BasePos+=tri.c*4096;
				runlistPos+=tri.a;
			}
		}
	}
	FileNode *IsolatedFile=new FileNode;
	IsolatedFile->MFTIndex=(Uint64)-1;
	IsolatedFile->parentIndex=5;
	IsolatedFile->IsDir=1;
	wstring name=DeleteEndBlank(AnsiToUnicode("孤立的文件和文件夹"));
	IsolatedFile->name=new wchar_t[name.length()+1];
	memcpy(IsolatedFile->name,name.c_str(),(name.length()+1)*sizeof(wchar_t));
	for (auto [mftindex,file]:AllFiles)
		if (mftindex!=file->parentIndex)
		{
			if (mftindex>=24&&file->name==nullptr)
			{
				wstring name=DeleteEndBlank(AnsiToUnicode("无名文件 "+llTOstr(mftindex)));
				file->name=new wchar_t[name.length()+1];
				memcpy(file->name,name.c_str(),(name.length()+1)*sizeof(wchar_t));
			}
			
			if (file->parentIndex==0&&mftindex>=24)
				IsolatedFile->childs.insert(mftindex);
			else if (AllFiles.find(file->parentIndex)==AllFiles.end())
				SendInfoMsg(1,"MFTRecord "+llTOstr(mftindex)+" 's parent node "+llTOstr(file->parentIndex)+" doesn't exsit. Filename:"+(file->name?DeleteEndBlank(UnicodeToUtf8(file->name)):""));
			else AllFiles[file->parentIndex]->childs.insert(mftindex);
		}
	DD[3]<<IsolatedFile->childs.size()<<endl;
	if (!IsolatedFile->childs.empty())
	{
		AllFiles[(Uint64)-1]=IsolatedFile;
		AllFiles[5]->childs.insert((Uint64)-1);
		for (auto sp:IsolatedFile->childs)
			AllFiles[sp]->parentIndex=(Uint64)-1;
	}
	else DeleteToNULL(IsolatedFile);
	DFSCompleteInfo(5,AllFiles[5]);
	return 0;
}

void SetCurrentPath(RecPath path,int from=0)//from:0:user 1:Pre 2:Nxt 3:refresh
{
	if (Recoverying)
	{
		ShowMsgBox(PUIT("数据恢复中，请勿操作!"),PUIT("警告"));
		return;
	}
	
	if (from==1)
	{
		PopHistory.push_back(CurrentPath);
		PathHistory.pop_back();
	}
	else if (from==2)
	{
		PathHistory.push_back(CurrentPath);
		PopHistory.pop_back();
	}
	else if (from!=3)
	{
		PathHistory.push_back(CurrentPath);
		PopHistory.clear();
	}
	
	SBV->ClearBlockContent();
	
	switch (path.type)//set path
	{
		case 0:
		{
			HANDLE h;
			for (int i=0,errorcnt=0;i<64&&errorcnt<2;++i)
				if (InThisSet(h=CreateFile(("\\\\.\\PhysicalDrive"+llTOstr(i)).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL),(HANDLE)NULL,INVALID_HANDLE_VALUE))
					++errorcnt;
				else
				{
					RecPath tar;
					tar.type=1;
					tar.name=PUIT("磁盘"+llTOstr(i));
					tar.DiskID=i;
					SBV->PushbackContent(SBVData(tar,tar.name));
					CloseHandle(h);
				}
			if (SBV->GetBlockCnt()==0)
				ShowMsgBox(PUIT("无法找到磁盘设备,请尝试使用管理员身份运行."),PUIT("警告"),SetSystemBeep_Warning);
			break;
		}
		case 1:
		{
			if (path.DiskID!=CurrentDiskID&&NotInSet(CurrentDrive,(HANDLE)NULL,INVALID_HANDLE_VALUE))
				CloseHandle(CurrentDrive),CurrentDrive=NULL,CurrentDiskID=-1,CurrentPartitionLBA=-1;
			Sector sec[34];
			if (InThisSet(CurrentDrive=CreateFile(("\\\\.\\PhysicalDrive"+llTOstr(path.DiskID)).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL),(HANDLE)NULL,INVALID_HANDLE_VALUE))
				ShowMsgBox(PUIT("无法打开该磁盘!"));
			else if (CurrentDiskID=path.DiskID,ReadSector(CurrentDrive,0,sec,34)!=34)
				ShowMsgBox(PUIT("读取头部扇区出错!"));
			else if (!((GPTHeader*)(sec+1))->SignatureCorrect())
			{
				//...
				ShowMsgBox(PUIT("非GPT分区表磁盘!"));
			}
			else
			{
				GPTEntries &partitions=*(GPTEntries*)(sec+2);
				for (int i=0;i<128;++i)
				{
					if (partitions[i].PartitionType.Empty()) break;
					RecPath tar;
					tar.type=2;
					tar.name=PUIT("分区"+llTOstr(i));
					tar.DiskID=CurrentDiskID;
					tar.partitionLBA=partitions[i].FirstLBA;
					SBV->PushbackContent(SBVData(tar,tar.name,"FirstLBA:"+ullTOpadic(partitions[i].FirstLBA),DeleteEndBlank(UnicodeToUtf8((wchar_t*)partitions[i].PartitionName))));
				}
			}
			break;
		}
		{
			FileNode *file;
		case 2:
			if (path.partitionLBA!=CurrentPartitionLBA)
			{
				for (auto [mftindex,file]:AllFiles)
					delete file;
				AllFiles.clear(); 
				DeleteToNULL(DataBitmap);
				CurrentPartitionLBA=path.partitionLBA;
				int ret=GetCurrentPartitionAllFile();
				if (ret==1||ret==3)
				{
					ShowMsgBox(PUIT(ret==1?"读取磁盘时发生了某些错误!":"当前分区非NTFS格式!"));
					CurrentPartitionLBA=-1;
					for (auto [mftindex,file]:AllFiles)
						delete file;
					AllFiles.clear();
					DeleteToNULL(DataBitmap);
					break;
				}
			}
			file=AllFiles[5];
		case 3:
			if (path.type==3)
				file=path.file;
			
			vector <SBVData> vec;
			for (auto sp:file->childs)
				if (sp>=24)
				{
					FileNode *p=AllFiles[sp];
					RecPath tar;
					tar.type=p->IsDir?3:4;
					tar.name=DeleteEndBlank(UnicodeToUtf8(p->name));
					tar.DiskID=CurrentDiskID;
					tar.partitionLBA=CurrentPartitionLBA;
					tar.file=p;
					SBVData sbvdata(tar,tar.name,
									PUIT((p->IsDir?"文件夹 | 总大小":"文件 | 大小")+GetFileSizeString(p->FileSize)+(p->IsDir&&p->HaveDeleted?" | 删除项大小 "+GetFileSizeString(p->DeletedSize):"")),
									PUIT(string(p->Deleted?"已删除":(p->HaveDeleted?"子项已删除":""))+(p->OccupiedCnt?" | 破损簇累计 "+llTOstr(p->OccupiedCnt):(p->BrokenCnt?" | 存在破损项":""))));
					sbvdata.SubTextColor2=RGBA_RED;
					if (p->IsDir)
					{
						sbvdata.BlockColor[0]=RGBA(255,240,203,255);
						sbvdata.BlockColor[1]=RGBA(255,223,182,255);
						sbvdata.BlockColor[2]=RGBA(255,197,146,255);
					}
					vec.push_back(sbvdata);
				}
			if (FileSortMode||SepareteDirFile)
				sort(vec.begin(),vec.end(),[](const SBVData &X,const SBVData &Y)->bool
				{
					RecPath x=X.FuncData,y=Y.FuncData;
					if (SepareteDirFile&&(x.file->IsDir!=y.file->IsDir))
						return x.file->IsDir;
					if (ReverseSort)
						swap(x,y);
					switch (FileSortMode)
					{
						default:
						case 0:	return x.file->MFTIndex<y.file->MFTIndex;
						case 1:	return SortComp_WithNum(x.name,y.name);
						case 2:	return x.file->FileSize>y.file->FileSize;
						case 3:	return x.file->DeletedSize>y.file->DeletedSize;
					}
				});
			for (auto vp:vec)
				SBV->PushbackContent(vp);
			break;
		}
	}
	CurrentPath=path;
}

SDL_TimerID Timer_Refresh=0;
Uint32 Timer_RefreshInterval=200;

FileNode *RecoveryFileNodePath=NULL;
bool RecoverNormal=0,RecoverBroken=0;
atomic_bool ForceThreadQuitFlag(0);
atomic_ullong TotalSizeToRecovery,RecoveredFileSize,TotalCountToRecovery,RecoveredFileCount;
//SDL_sem *Sem_ConfirmMsg=NULL;
//int LastConfirmChoice=0;

void DFS_Recovery(wstring fapath,FileNode *u)
{
	if (ForceThreadQuitFlag) return;
	fapath+=wstring(L"\\")+u->name;
	string path=DeleteEndBlank(UnicodeToUtf8(fapath));
	if (!u->IsDir&&u->OccupiedCnt)
		ThreadSendInfoMsg(1,PUIT("文件 \"")+path+PUIT(string("\" 可能已破损! ")+(RecoverBroken?"其导出的数据可能存在错误.":"不对其恢复.")));
	if (u->MFTIndex>=24&&u->fa!=u&&u->fa!=NULL&&u->IsDir&&(u->HaveDeleted||RecoverNormal))
	{
		int x=GetFileAttributesW(fapath.c_str());
		if (x==-1||!(x&FILE_ATTRIBUTE_DIRECTORY))
			if (CreateDirectoryW(fapath.c_str(),NULL))
				ThreadSendInfoMsg(0,PUIT("创建文件夹 \"")+path+PUIT("\" 成功."));
			else
			{
				ThreadSendInfoMsg(2,PUIT("创建文件夹 \"")+path+PUIT("\" 失败！错误码:"+llTOstr(GetLastError())),SetSystemBeep_Error);
				//...
				return;
			}
		++RecoveredFileCount;
	}
	else if (u->MFTIndex>=24&&!u->IsDir&&(u->Deleted&&(RecoverBroken||u->OccupiedCnt==0)||RecoverNormal))
	{
		HANDLE h=CreateFileW(fapath.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,0,NULL);
		if (InThisSet(h,(HANDLE)NULL,INVALID_HANDLE_VALUE))
		{
			ThreadSendInfoMsg(2,PUIT("创建文件 \"")+path+PUIT("\" 失败！错误码:"+llTOstr(GetLastError())),SetSystemBeep_Error);
			//...
			return;
		}
		else ThreadSendInfoMsg(0,PUIT("创建文件 \"")+path+PUIT("\" 成功."));
		unsigned long long len=u->FileSize;
		DWORD cnt=0;
		if (u->data!=NULL)
		{
			if (!WriteFile(h,u->data,len,&cnt,NULL)||cnt<len)
			{	
				ThreadSendInfoMsg(2,PUIT("无法写入文件 \"")+path+PUIT("\" ！错误码:"+llTOstr(GetLastError())),SetSystemBeep_Error);
				//...
				return;
			}
		}
		else
		{
			unsigned long long pos=CurrentPartitionLBA<<9;
			const unsigned long long clustersize=4096ull;
			Sector buffer[8];
			for (auto vp:u->dataruns)
			{
				pos+=vp.b*4096ull;
				for (long long i=0;i<vp.a&&len&&!ForceThreadQuitFlag;++i)
				{
					if (ReadSector(CurrentDrive,pos+i*clustersize>>9,buffer,8)!=8)
					{
						ThreadSendInfoMsg(2,PUIT("读取扇区 "+ullTOpadic(pos+i*clustersize>>9)+"H 失败！错误码:"+llTOstr(GetLastError())),SetSystemBeep_Error);
						//...
						return;
					}
					if (!WriteFile(h,buffer,min(clustersize,len),&cnt,NULL)||cnt<min(clustersize,len))
					{
						ThreadSendInfoMsg(2,PUIT("无法写入文件 \"")+path+PUIT("\" ！错误码:"+llTOstr(GetLastError())),SetSystemBeep_Error);
						//...
						return;
					}
					RecoveredFileSize+=min(len,clustersize);
					len-=min(len,clustersize);
				}
				if (len==0||ForceThreadQuitFlag)
					break;
			}
			if (!ForceThreadQuitFlag&&len!=0)
				ThreadSendInfoMsg(2,PUIT("文件 \"")+path+PUIT("\" 恢复不完整！剩余大小:"+GetFileSizeString(len)),SetSystemBeep_Error);
		}
		CloseHandle(h);
		++RecoveredFileCount;
	}
	if (ForceThreadQuitFlag)
		return;
	
	for (auto sp:u->childs)
		DFS_Recovery(fapath,AllFiles[sp]);
}

int Thread_RecoveryFile(void *data)
{
	string *fapath=(string*)data;
	TotalSizeToRecovery=RecoverNormal?RecoveryFileNodePath->FileSize:RecoveryFileNodePath->DeletedSize;
	RecoveredFileSize=0;
	TotalCountToRecovery=RecoverNormal?RecoveryFileNodePath->TreeSize:RecoveryFileNodePath->HaveDeleted;
	RecoveredFileCount=0;
	DFS_Recovery(DeleteEndBlank(Utf8ToUnicode(*fapath)),RecoveryFileNodePath);
	if (!ForceThreadQuitFlag)
		PUI_SendFunctionEvent<string>([](string &fapath,int)->int
		{
			SetSystemBeep(SetSystemBeep_Notification);
			auto mbb=new MessageBoxButton <string> (0,PUIT("提示"),PUIT("恢复完成."));
			mbb->SetButtonWidth(120);
			mbb->AddButton(PUIT("确定"));
			mbb->AddButton(PUIT("打开目标位置"),[](string &fapath)
			{
				SelectInWinExplorer(fapath);
			},fapath);
			SendInfoMsg(4,PUIT("提示:恢复完成."),SetSystemBeep_Notification);
			Recoverying=0;
			return 0;
		},*fapath);
	else PUI_SendFunctionEvent([](void*)
		{
			ThreadShowMsgBox(PUIT("已中止数据恢复."),PUIT("提示"),SetSystemBeep_Notification);
			ForceThreadQuitFlag=0;
			Recoverying=0;
		});
	delete fapath;
	return 0;
}

void RecoveryFileNodeTo(const string &tarpath)
{
	if (Recoverying)
	{
		ShowMsgBox(PUIT("请等待当前数据恢复完成."),PUIT("提示"));
		return;
	}
	Recoverying=1;
	SDL_Thread *Th_RecoveryFile=SDL_CreateThread(Thread_RecoveryFile,"RecoveryFile",new string(tarpath));
	SDL_DetachThread(Th_RecoveryFile);
}

void SetRecoveryFileNode(FileNode *tar)
{
	if (Recoverying)//??
	{
		ShowMsgBox(PUIT("数据恢复中，请勿操作!"),PUIT("警告"));
		return;
	}
	RecoveryFileNodePath=tar;
	new SimpleFileSelectBox(0,[](const string &str)->int
	{
		RecoveryFileNodeTo(str);
		return 0;
	},"",{"dir"},600,400,PUIT("请选择恢复到的目录:"));
}

void RightClickFileNode(const RecPath &path)
{
	vector <MenuData<RecPath> > menudata;
	
	if (path.type==3||path.type==4)
	{
		menudata.push_back(MenuData<RecPath>(PUIT("恢复到"),[](RecPath &path)
		{
			SetRecoveryFileNode(path.file);
		},path));
		
		menudata.push_back(MenuData<RecPath>(PUIT("当前目录下全部恢复到"),[](RecPath &path)
		{
			SetRecoveryFileNode(path.file->fa);
		},path));
		
		menudata.push_back(MenuData<RecPath>(0));
	}
	
	menudata.push_back(MenuData<RecPath>(PUIT("详情"),[](RecPath &path)
	{
		switch (path.type)
		{
			case 3:	case 4:
			{
				MessageBoxLayer *mbl=new MessageBoxLayer(0,PUIT("详情"),500,600);
				mbl->EnableShowTopAreaColor(1);
				mbl->SetClickOutsideReaction(1);
				mbl->SetBackgroundColor({255,255,255,200});
				int y=10;
				#define DisplayItem(A,B)															\
					{																				\
						new TinyText(0,mbl,new PosizeEX_Fa6(3,3,30,100,y+=40,30),PUIT(A),1);	\
						new TinyText(0,mbl,new PosizeEX_Fa6(2,3,150,30,y,30),(B),-1);		\
					}
				DisplayItem("名称:",path.name);
				DisplayItem("类型:",PUIT(path.type==3?"文件夹":"文件"));
				DisplayItem("所在磁盘ID:",llTOstr(path.DiskID));
				DisplayItem("所在分区LBA:",ullTOpadic(path.partitionLBA)+"H");
				DisplayItem("MFT编号:",llTOstr(path.file->MFTIndex));
				DisplayItem("父目录MFT编号",llTOstr(path.file->parentIndex));
				DisplayItem("文件(夹)大小:",GetFileSizeString(path.file->FileSize));
				DisplayItem("删除状态:",PUIT(path.file->Deleted?"已删除":"正常"));
				DisplayItem("次级结点个数:",llTOstr(path.file->childs.size()));
				DisplayItem("子树大小:",llTOstr(path.file->TreeSize));
				DisplayItem("数据存储类型:",PUIT(path.file->data==NULL?"RunList":"MFT"));
				if (!path.file->IsDir)
					new Button<RecPath>(0,mbl,new PosizeEX_Fa6(1,3,60,30,y,30),PUIT("查看"),
						[](RecPath &path)
						{
							MessageBoxLayer *mbl=new MessageBoxLayer(0,PUIT("数据详情"),500,350);
							mbl->EnableShowTopAreaColor(1);
							mbl->SetClickOutsideReaction(1);
							mbl->SetBackgroundColor({255,255,255,200});
							SimpleTextBox *STB=new SimpleTextBox(0,mbl,new PosizeEX_Fa6(2,2,30,30,40,20));
							if (path.file->data==NULL)
								for (auto vp:path.file->dataruns)
									STB->AppendNewLine(llTOstr(vp.a)+" : "+llTOstr(vp.b));
							else
							{
								string s;
								for (int i=0;i<path.file->FileSize;++i)
									s+=ullTOpadic(path.file->data[i])+"  ";
								STB->SetText(s);
							}
						},path);
				DisplayItem("子树删除项数目:",llTOstr(path.file->HaveDeleted));
				DisplayItem("子树删除项大小:",GetFileSizeString(path.file->DeletedSize));
				#undef DisplayItem
				break;
			}
		}
	},path));
	
	if (!menudata.empty())
		new Menu1<RecPath>(0,menudata);
}

void InitUI()
{
	DEBUG_EnableDebugThemeColorChange=1;
//	DEBUG_EnableForceQuitShortKey=2;
	
	TwinLay=new TwinLayerWithDivideLine(0,PUI_FA_MAINWINDOW,new PosizeEX_Fa6_Full,1,0.75);
	TwinLay->SetDivideLineMode(2,-0.5,300);
	Bu_Pre=new ButtonI(0,TwinLay->AreaA(),new PosizeEX_Fa6(3,3,0,35,0,30),"<",
		[](int&)
		{
			if (!PathHistory.empty())
				SetCurrentPath(PathHistory.back(),1);
		},0);
	Bu_Nxt=new ButtonI(0,TwinLay->AreaA(),new PosizeEX_Fa6(3,3,36,35,0,30),">",
		[](int&)
		{
			if (!PopHistory.empty())
				SetCurrentPath(PopHistory.back(),2);
		},0);
	SBV=new SimpleBlockView <RecPath> (0,TwinLay->AreaA(),new PosizeEX_Fa6(2,2,5,5,35,5),[](RecPath &path,int pos,int click)
	{
		if (pos==-1) return;
		if (click==2)
			if (path.type==4)
				DoNothing;
			else SetCurrentPath(path);
		else if (click==3)
			RightClickFileNode(path);
	});
	SBV->SetEachBlockPosize({5,5,280,80});
	SBV->SetEnablePic(0);
	ProBar=new ProgressBar(0,TwinLay->AreaA(),new PosizeEX_Fa6(2,3,75,0,0,30));
	TT_RecoveryState=new TinyText(0,ProBar,new PosizeEX_Fa6_Full,PUIT("未进行恢复."),0);
	Bu_Title=new ButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,0,0,0,30),ProgramName+"-"+ProgramVersion+" By:qianpinyi",
		[](int&)
		{
			auto mbl=new MessageBoxLayer(0,PUIT("关于 "+ProgramName),400,250);
			mbl->EnableShowTopAreaColor(1);
			mbl->SetClickOutsideReaction(1);
			
			new TinyText(0,mbl,new PosizeEX_Fa6(2,3,30,30,50,30),PUIT("当前版本: ")+ProgramVersion);
			new TinyText(0,mbl,new PosizeEX_Fa6(2,3,30,30,80,30),PUIT("开发者: qianpinyi"));
			new TinyText(0,mbl,new PosizeEX_Fa6(2,3,30,30,110,30),PUIT("创建日期: 21.11.25"));
			new TinyText(0,mbl,new PosizeEX_Fa6(2,3,30,30,140,30),PUIT("当前版本日期: ")+ProgramVersionDate);
			auto Bu_mail=new Button <Widgets*> (0,mbl,new PosizeEX_Fa6(2,3,30,30,170,30),PUIT("开发者邮箱: qianpinyi@outlook.com"),
				[](Widgets *&funcdata)->void
				{
					ShellExecuteW(0,L"open",L"mailto:qianpinyi@outlook.com",L"",L"",SW_SHOWNORMAL);
					((Button<Widgets*>*)funcdata)->SetTextColor({0,100,255,255});
				},NULL);
			Bu_mail->GetFuncData()=Bu_mail;
			Bu_mail->SetButtonColor(0,RGBA_TRANSPARENT);
		},0);
	Bu_Title->SetButtonColor(0,RGBA_TRANSPARENT);
	Bu_Title->SetButtonColor(1,ThemeColorM[0]);
	Bu_Title->SetButtonColor(2,ThemeColorM[1]);
	Bu_Title->SetTextColor(ThemeColorM[7]);
	DDB_Sort=new DropDownButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,30,40,30),PUIT("排序方式:默认"),
		[](int&,int pos)
		{
			if (!InRange(pos,0,3)||pos==FileSortMode)
				return;
			FileSortMode=pos;
			SetCurrentPath(CurrentPath,3);
		});
	DDB_Sort->PushbackChoiceData(PUIT("排序方式:默认"),0);
	DDB_Sort->PushbackChoiceData(PUIT("排序方式:文件名"),1);
	DDB_Sort->PushbackChoiceData(PUIT("排序方式:大小"),2);
	DDB_Sort->PushbackChoiceData(PUIT("排序方式:删除项大小"),3);
	new TinyText(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,90,80,30),PUIT("排序区分文件夹和文件"),-1);
	new SwitchButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(1,3,50,30,85,20),0,[](int&,bool onoff)
	{
		SepareteDirFile=onoff;
		SetCurrentPath(CurrentPath,3);
	},0);
	new TinyText(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,90,120,30),PUIT("反向排序"),-1);
	new SwitchButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(1,3,50,30,125,20),0,[](int&,bool onoff)
	{
		ReverseSort=onoff;
		SetCurrentPath(CurrentPath,3);
	},0);
	new TinyText(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,90,160,30),PUIT("一并恢复正常文件"),-1);
	auto sb_recnormal=new SwitchButtonI(1,TwinLay->AreaB(),new PosizeEX_Fa6(1,3,50,30,165,20),0,[](int&,bool onoff)
	{
		if (Recoverying)
		{
			ShowMsgBox(PUIT("数据恢复中，请勿操作!"),PUIT("警告"));
			((SwitchButtonI*)Widgets::GetWidgetsByID(1))->SetOnOff(RecoverNormal,0);
		}
		RecoverNormal=onoff;
	},NULL);
	new TinyText(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,90,200,30),PUIT("\"恢复\"可能的破损文件"),-1);
	auto sb_recbroken=new SwitchButtonI(2,TwinLay->AreaB(),new PosizeEX_Fa6(1,3,50,30,205,20),0,[](int&,bool onoff)
	{
		if (Recoverying)
		{
			ShowMsgBox(PUIT("数据恢复中，请勿操作!"),PUIT("警告"));
			((SwitchButtonI*)Widgets::GetWidgetsByID(2))->SetOnOff(RecoverBroken,0);
		}
		RecoverBroken=onoff;
	},NULL);
	new ButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,30,240,30),PUIT("<全部恢复>"),
		[](int&)
		{
			if (CurrentPath.type==2)
				SetRecoveryFileNode(AllFiles[5]);
			else if (CurrentPath.type==3)
				SetRecoveryFileNode(CurrentPath.file);
		},0);
	new ButtonI(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,30,280,30),PUIT("<中止当前数据恢复！>"),
		[](int&)
		{
			if (Recoverying)
				ForceThreadQuitFlag=1;
			else ShowMsgBox(PUIT("未进行数据恢复，无需中止."),PUIT("提示"));
		},0);
	auto InfoBorder=new BorderRectLayer(0,TwinLay->AreaB(),new PosizeEX_Fa6(2,3,30,30,320,380));
	STB_Info=new SimpleTextBox(0,InfoBorder,new PosizeEX_Fa6_Full);
	
	PUI_UpdateWidgetsPosize();

	Timer_Refresh=SDL_AddTimer(Timer_RefreshInterval,[](Uint32 itv,void*)->Uint32
	{
		PUI_SendFunctionEvent([](void*)
		{
			double newPer=RecoveredFileSize*1.0/TotalSizeToRecovery;
			if (TotalCountToRecovery!=0&&TotalSizeToRecovery!=0&&ProBar->GetPercent()!=newPer)
			{
				ProBar->SetPercent(newPer);
				TT_RecoveryState->SetText(PUIT("恢复中("+llTOstr(RecoveredFileSize*100.0/TotalSizeToRecovery)+"%) | 项数: "+llTOstr(RecoveredFileCount)+"/"+llTOstr(TotalCountToRecovery)+" | 大小: "+GetFileSizeString(RecoveredFileSize)+"/"+GetFileSizeString(TotalSizeToRecovery)));
			}
		});
		return itv;
	},NULL);
}

int main(int argc,char **argv)
{
	ProgramPath=GetPreviousBeforeBackSlash(argv[0]);
	SetCmdUTF8AndPrintInfo(ProgramName,ProgramVersion,"qianpinyi");
	DD.SetLOGFile(ProgramPath+"\\Log.txt");
	DD%DebugOut_CERR_LOG;
	PUI_SetPreferredRenderer(PUI_PreferredRenderer_OpenGL);
	PAL_GUI_Init(PUI_WINPS_DEFAULT,ProgramName+"-"+ProgramVersion);
	InitUI();
	SetCurrentPath(RecPath());
	SendInfoMsg(5,PUIT("本软件存在一些未完成的功能和可能的Bug，请务必了解数据恢复的注意点并小心使用!"));
	PUI_EasyEventLoop([](const PUI_Event *event,int &quitflag)
	{
		if (quitflag&&Recoverying)
		{
			quitflag=0;
			ShowMsgBox(PUIT("正在恢复文件，请等待完成或手动中止!"),PUIT("警告"));
		}
	});
	if (CurrentDrive!=NULL)
		CloseHandle(CurrentDrive);
	SDL_RemoveTimer(Timer_Refresh);
	PAL_GUI_Quit();
	return 0;
}
