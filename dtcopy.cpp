/*$
	dtcopy
	Copia condizionalmente, in base a data/ora, includendo una minima gestione delle versioni.
	Luca Piergentili, 01/06/25
*/
#include "pragma.h"
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "strings.h"
#include "typeval.h"
#include "windows.h"
#include "win32api.h"
#include "getopt.h"
#include "CWildCards.h"
#include "CGzw.h"
#include "CNodeList.h"
#include "CRegistry.h"
#include "CFindFile.h"
#include "CSync.h"
#include "CFifoLifo.h"

// per SH...()
#include <shlobj.h>

// per .wav
#include "resource.h"
#include <mmsystem.h>
#pragma comment(lib,"WinMM.lib")
#ifdef PRAGMA_MESSAGE_VERBOSE
  #pragma message("\t\t\t"__FILE__"("STR(__LINE__)"): automatically linking with WinMM")
#endif

#define DTCOPY_PROJECT_HOME "https://github.com/lpierge/dtcopy"

/*
	prototipi
*/
DWORD		CopyDirectory			(LPCSTR lpcszSrcDir,LPCSTR lpcsDstDir,BOOL bRecursiveyCopy,CWildCards* pWildCards,BOOL bMatchWhole,int& nCopiedFiles,int& nUnchangedFiles,int& nExcludedFiles,BOOL bShowExcluded,CDateTime& dateTime,DWORD& dwTot);
bool		DoesMatchWithPatterns	(const char* name,CWildCards* pWildCards);
void		PlayEmbeddedWave		(UINT nResourceID);
LONG		GzwCallback				(WPARAM wParam,LPARAM lParam1,LPARAM lParam2,LPARAM lParam3);
BOOL WINAPI CtrlHandler				(DWORD dwCtrlType);

/*
	globali
*/
BOOL g_bInterrupted = FALSE;

/*
	main()
*/
int main(int argc,char* argv[])
{
	InitConsoleGeometry(120,9000);

	// imposta l'handler per il Ctrl+C
	if(!SetConsoleCtrlHandler(CtrlHandler,TRUE))
	{
		printf("error: %ld, unable to set the Ctrl+C handler\n",GetLastError());
		Beep(1000,300);
		return(1);
	}
	
	printf(	"dtcopy v2.7.1 (compiled %s)\n"\
			"Date/Time conditional directory copy.\n"\
			"A command-line utility for timestamp-conditional directory copies, with optional compressed (.gzw) versioning support.\n"\
			"Written by LPI.\n"\
			"GZW format is based on the zLib version 1.1.3.\n"\
			"zLib is copyright (C) 1995-1998 by Jean-loup Gailly and Mark Adler.\n"\
			"Project hosted at %s\n"
			"\n",
			__DATE__,
			DTCOPY_PROJECT_HOME
			);

	// opzioni da linea di comando   
    #define GETOPT_s    0			// <-s<directory>>		source directory
    #define GETOPT_d    1			// <-d<directory>>		destination directory
    #define GETOPT_r    2			// [-r]					copy recursively
	#define GETOPT_q    3			// [-q<date|current>]	quick copy only modified files since [dd/mm/yyyy], no check against destination
    #define GETOPT_x    4			// [-x<exclusions>]		exclude list, items separated by ;
    #define GETOPT_X    5			// [-X]					show exclusions
    #define GETOPT_f	6			// [-f]					match wildcards with the filename
    #define GETOPT_F	7			// [-F]					match wildcards with the full path
    #define GETOPT_y    8			// [-y<exclusions>]		exclude list for the gzw api, items separated by ; if none -x will be used
    #define GETOPT_v    9			// [-v[version]]		version number for the compressed copy of the source directory
    #define GETOPT_V    10			// [-V<directories>]	make a compressed copy of the additional directories, separated by ;
    #define GETOPT_l    11			// [-l<gzwfile>]		list the .gzw file content
    #define GETOPT_g    12			// [-g<gzwfile>]		extract the .gzw file to the destination (-d<...>) directory
    #define GETOPT_h    13			// [-h]					show help

    GETOPT opts[] = {
	{'s',0,1,string_type},
	{'d',0,1,string_type},
	{'r',0,0,chr_type},
	{'q',0,1,string_type},
	{'x',0,1,string_type},
	{'X',0,0,chr_type},
	{'f',0,0,chr_type},
	{'F',0,0,chr_type},
	{'y',0,1,string_type},
	{'v',0,1,string_type},
	{'V',0,1,string_type},
	{'l',0,1,string_type},
	{'g',0,1,string_type},
	{'h',0,0,chr_type}
	};
    
    // analizza la linea di comando
	char szWrongOpts[MAX_CMDLINE+1] = {0};
	int nRet = getopt('-',&opts[0],sizeof(opts)/sizeof(opts[0]),argc,argv,szWrongOpts,sizeof(szWrongOpts),NULL);
	BOOL bResult = FALSE;
	switch(nRet)
	{
		case -2:
			printf("error: command line too long\n");
			break;
		case -1:
			printf("error: no args received, use -h for help\n");
			break;
		case 0:
			bResult = TRUE;
			break;
		default: // >0
			if(*szWrongOpts)
				printf("error: wrong use of the \'%s\' option(s)/argument(s), use -h for help\n",szWrongOpts);
			else
				printf("error: wrong use of option(s)/argument(s), use -h for help\n");
			break;
	}
	if(!bResult)
		return(1);

    if(opts[GETOPT_h].bFound)
    {
		/*	esempi:

			- duplica (condizionalmente) una directory da C: a D:, escludendo le (sub)dir ".vs", "Debug" y "Release", pero' 
			occhio, perche' i valori di esclusione vengono ricercati come sottostringhe, quindi "Debug" escluderebbe anche 
			il  file "copydebug.bat", che si trovi o meno nella (sub)dir Debug:
			
				C:\BIN\dtcopy -r -s"C:\DEV" -d"D:\DEV" -x.vs;Debug;Release -X
			
			per ovviare, usare quindi la forma (che esclude anche qualsiasi file .exe e .obj):

				C:\BIN\dtcopy -r -s"C:\DEV" -d"D:\DEV" -x\.vs\;\Debug\;\Release\;*.exe;*.obj -X -F

			notare che in tal caso l'opzione -F e' fondamentale per far si' che la ricerca avvenga sull'intero pathname, dato
			che con l'opzione -f (il default) viene effettuato il confronto unicamente sul nome file, non sul path, ed un file
			con i caratteri '\' ovviamente non esiste

			- copia (condizionalmente) la directory del progetto dtcopy da C: a D:, esclude le (sub)dir ".vs", "Debug" e "Release" 
			ed effettua una copia delle directories "Library" e "Include", creando un .gzw per ognuno dei tre progetti ed 
			etichettandolo con il numero di versione specificato
			in altre parole, versiona il progetto dtcopy tirandosi appresso il codice addizionale che questo richiede, ossia le 
			directory L:\Library e L:\Include
			occhio al flag -F che specifica verificare le esclusioni sul pathname completo, per default (-f) la verifica e' solo 
			sul nome file (vedi l'esempio precedente):
		
				C:\BIN\dtcopy -r -s"C:\DEV\dtcopy" -d"D:\DEV\VERSIONS\dtcopy" -x\.vs\;\Debug\;\Release\ -X -F -v2.5 -VL:\Library;L:\Include

			- effettua una copia 'fake' ("C:\DEV\dummy" contiene un file fittizio) per poter versionare l'intera directory C:\DEV, 
			ossia per poter effettuare un backup di tutta la directory C:\DEV in un unico .gzw di output:

				C:\BIN\dtcopy -r -s"C:\DEV\dummy" -d"D:\DEV\VERSIONS" -x\.vs\;\Debug\;\Release\ -F -X -v1.0 -VC:\DEV
		*/
		printf(	"usage:\tdtcopy <-s<input>> <-d<output>> [options]\n\t"\
				"<-s<input>>           source directory\n\t"\
				"<-d<output>>          destination directory\n\t"\
				"[-r]                  copy recursively\n\t"\
				"[-q<date|current>]    quick copy only modified files since <date> (in dd/mm/yyyy format), no check against destination\n\t"\
				"                      use \"current\" to use the current date\n\t"\
				"[-x<exclusions>]      skeleton/pattern for file/dir exclusions, items separated by ;\n\t"\
				"[-X]                  show excluded file/dir\n\t"\
				"[-f]                  match the skeleton/pattern with the file name only (default)\n\t"\
				"[-F]                  match the skeleton/pattern with the whole pathname\n\t"\
				"[-y<exclusions>]      skeleton/pattern for file/dir exclusions for the gzw api, items separated by ;\n\t"\
				"                      if none, -x value will be used\n\t"\
				"[-V<input>]           make a compressed (.gzw) copy of the additional directories, separated by ;\n\t"\
				"[-v[version]]         version number for the compressed copy of the additional directories (mandatory with -V)\n\t"\
				"[-l<filename>]        list the .gzw file content\n\t"\
				"[-g<filename>]        extract the .gzw file to the destination directory\n\t"\
				"[-h]                  this help\n\t"\
				"\n\tnotes:"\
	 			"\n\t- 'skeleton' is used as a synonym for 'exact pattern' (with no wildcards),"\
	 			"\n\t  while 'pattern' refers to a string that may contain wildcards"\
	 			"\n\t- <...> means mandatory, while [...] means optional\n\t"\
				"\n\tsamples:"\
				"\n"\
	 			"\n\tdtcopy -r -s\"C:\\DEV\" -d\"D:\\DEV\" -x.vs;Debug;Release -X -f"\
	 			"\n\t\tcopy recursively anything new from \"C:\\DEV\" to \"D:\\DEV\","\
	 			"\n\t\texclude all files containing any of these substrings: \".vs\", \"Debug\", \"Release\""\
	 			"\n\t\t(matching only the filename: -f option)"\
	 			"\n\t\tand show excluded files"\
				"\n"\
	 			"\n\tdtcopy -r -s\"C:\\DEV\" -d\"D:\\DEV\" -x\\.vs\\;\\Debug\\;\\Release\\;*.exe;*.obj -X -F"\
	 			"\n\t\tcopy recursively anything new from \"C:\\DEV\" to \"D:\\DEV\","\
	 			"\n\t\texclude all directories containing any of these substrings: \".vs\", \"Debug\", \"Release\""\
	 			"\n\t\tand exclude all files following these patterns: \"*.exe\", \"*.obj\""\
	 			"\n\t\t(matching the whole pathname: -F option)"\
	 			"\n\t\tand show excluded files"\
				"\n"\
	 			"\n\tdtcopy -r -s\"C:\\DEV\\dtcopy\" -d\"D:\\DEV\\VERSIONS\\dtcopy\" -x\\.vs\\;\\Debug\\;\\Release\\ -X -F -v2.5 -VL:\\Library;L:\\Include"\
	 			"\n\t\tcopy recursively anything new from \"C:\\DEV\\dtcopy\" to \"D:\\DEV\\VERSIONS\\dtcopy"\
	 			"\n\t\texclude all directories containing any of these substrings: \".vs\", \"Debug\", \"Release\""\
	 			"\n\t\t(matching the whole pathname: -F option)"\
	 			"\n\t\tcreate a .gzw compressed file, labeled 2.5, containing the dtcopy project"\
	 			"\n\t\tand more .gzw compressed files, one for each of the directories specified with the -V option"\
				"\n\t\t(the dependencies of dtcopy)"\
	 			"\n\t\tand show excluded files"\
				"\n"\
	 			"\n\tdtcopy -l\"D:\\DEV\\VERSIONS\\dtcopy\\dtcopy.2.5.gzw\""\
	 			"\n\t\tlist the content of the .gzw file"\
				"\n"\
	 			"\n\tdtcopy -g\"D:\\DEV\\VERSIONS\\dtcopy\\dtcopy.2.5.gzw\" -d\"C:\\TMP\\dtcopy\""\
	 			"\n\t\textract the content of the .gzw file into the \"C:\\TMP\\dtcopy\" directory"\
				"\n"\
	 			"\n\tdtcopy -r -s\"C:\\DEV\\dummy\" -d\"D:\\DEV\\VERSIONS\" -x\\.vs\\;\\Debug\\;\\Release\\ -F -X -v1.0 -VC:\\DEV"\
	 			"\n\t\tcreates a 'fake' copy (the folder C:\\DEV\\dummy contains a dummy file) in order to version the entire C:\\DEV directory,"\
	 			"\n\t\tenabling a full backup of one folder (C:\\DEV) into a single .gzw output file (D:\\DEV\\VERSIONS\\DEV.1.0.gzw)"\
				"\n"\
	 			"\n\tdtcopy -q\"current\" -r -s\"C:\\DEV\" -d\"D:\\DEV\""\
	 			"\n\t\trecursively copies from C:\\DEV to D:\\DEV only files modified today (-q option with \"current\" argument),"\
				"\n\t\twithout comparing them against the date/time of the destination files."\
				"\n"\
	 			"\n\tdtcopy -q\"26/08/2025\" -r -s\"C:\\DEV\" -d\"D:\\DEV\""\
	 			"\n\t\trecursively copies from C:\\DEV to D:\\DEV only files modified starting from August 26, 2025 (-q option with \"dd/mm/yyyy\" argument),"\
				"\n\t\twithout comparing them against the date/time of the destination files."\
				"\n"
				);

        return(1);
    }

	int i = 0;
    LPCSTR lpcszInputDir = NULL;
    LPCSTR lpcszOutputDir = NULL;
	CWildCards wildCards;
	wildCards.SetIgnoreCase(TRUE);
	wildCards.SetIgnoreSpaces(TRUE);
	CItemList* pitemListExl = NULL;
	BOOL bMatchWhole = FALSE;
	char szDirectoriesToBeVersioned[STR_MAX_VALUE+1] = {0}; // stesso limite di: opts[GETOPT_V].uValue.szValue
	CDateTime dateTime;
	dateTime.SetDate();
	dateTime.SetTime();

	// -l<...>
	// lista il file .gzw e termina
    if(opts[GETOPT_l].bFound)
    {
		// controlla che venga specificato il file .gzw
		if(!*opts[GETOPT_l].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_l].cOpt);
	        return(1);
		}
		// controlla che il file .gzw esista
		char szGzwName[_MAX_FILEPATH+1] = {0};
		strcpyn(szGzwName,opts[GETOPT_l].uValue.szValue,sizeof(szGzwName));
		if(!FileExists(szGzwName))
		{
			printf("error: no such file: %s\n",szGzwName);
	        return(1);
		}
		// visualizza il contenuto del file .gzw
		FPGZWCALLBACK pGzwCallback = GzwCallback;
		CGzw gzw;
		gzw.SetCallback(pGzwCallback);
		gzw.SetOperation(GZW_LIST);
		gzw.SetInput(szGzwName);
		UINT nRet = gzw.Gzw();
		printf("\nlisting ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);
		return(nRet==GZW_SUCCESS ? 0 : 1);
    }

	// -g<...>
	// estrae il file .gzw e termina
    if(opts[GETOPT_g].bFound)
    {
		// controlla che venga specificato il file .gzw
		if(!*opts[GETOPT_g].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_g].cOpt);
	        return(1);
		}
		// controlla che venga specificata la directory in cui estrarre
		if(!*opts[GETOPT_d].uValue.szValue)
		{
			printf("error: you must specify a destination directory to extract the file\n");
	        return(1);
		}
		DWORD dwError = 0L;
		if(DoesFileExist(opts[GETOPT_d].uValue.szValue,&dwError))
		{
			printf("error: you cannot specify a file as a destination to extract the file\n");
	        return(1);
		}
		// controlla che il file .gzw esista
		char szGzwName[_MAX_FILEPATH+1] = {0};
		strcpyn(szGzwName,opts[GETOPT_g].uValue.szValue,sizeof(szGzwName));
		if(!FileExists(szGzwName))
		{
			printf("error: no such file: %s\n",szGzwName);
	        return(1);
		}
		// estrae il file .gzw ricreando il pathname relativo sulla directory di destinazione
		FPGZWCALLBACK pGzwCallback = GzwCallback;
		CGzw gzw;
		UINT nRet = GZWE_UNKNOWN_ERROR;
		gzw.SetCallback(pGzwCallback);
		gzw.SetOperation(GZW_UNCOMPRESS);
		gzw.SetOverwrite(TRUE);
		gzw.SetPathScheme(GZW_PATHSCHEME_RELATIVE);
		gzw.SetPatternMatchWhole(TRUE);
		gzw.SetInput(szGzwName);
		gzw.SetOutput(opts[GETOPT_d].uValue.szValue);
		printf("%s will be extracted into %s overwriting existing files, are you sure (y/n)",szGzwName,opts[GETOPT_d].uValue.szValue);
		if(ConsolePromptYesOrNo()==YES)
		{
			printf("\n");
			nRet = gzw.Gzw();
			printf("\nextraction ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);
		}
		return(nRet==GZW_SUCCESS ? 0 : 1);
    }

	// -s<...>
	// directory sorgente, se viene specificato -l/-g (lista/estrazione .gzw) la ignora
	// in ogni caso, -l/-g terminano prima di arrivare qui
	if(!opts[GETOPT_l].bFound && !opts[GETOPT_g].bFound)
	{
		if(opts[GETOPT_s].bFound)
		{
			if(!*opts[GETOPT_s].uValue.szValue)
			{
				printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_s].cOpt);
				return(1);
			}
		}
		else
		{
			printf("error: you must specify a source directory for the copy\n");
			return(1);
		}
		// normalizza la directory sorgente, NON deve terminare con '\'
		i = strlen(opts[GETOPT_s].uValue.szValue)-1;
		if(opts[GETOPT_s].uValue.szValue[i]=='\\')
			opts[GETOPT_s].uValue.szValue[i] = '\0';
		// controlla che la directory sorgente esista
		lpcszInputDir = opts[GETOPT_s].uValue.szValue;
		DWORD dwInputAttribute = GetFileAttributes(lpcszInputDir);
		if(dwInputAttribute==INVALID_FILE_ATTRIBUTES || !(dwInputAttribute & FILE_ATTRIBUTE_DIRECTORY))
		{
			printf("%s invalid source directory\n",lpcszInputDir);
			return(1);
		}
	}

	// -d<...>
	// directory di destinazione, dove copiare i files o estrarre il .gzw
    if(opts[GETOPT_d].bFound)
    {
		if(!*opts[GETOPT_d].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_d].cOpt);
	        return(1);
		}
    }
	else
	{
		printf("error: you must specify a destination directory for the copy\n");
        return(1);
	}
	// normalizza la directory di destinazione, NON deve terminare con '\'
	i = strlen(opts[GETOPT_d].uValue.szValue)-1;
	if(opts[GETOPT_d].uValue.szValue[i]=='\\')
		opts[GETOPT_d].uValue.szValue[i] = '\0';
	// crea la directory di destinazione
    lpcszOutputDir = opts[GETOPT_d].uValue.szValue;
	DWORD dwError = 0L;
	if(!DoesDirectoryExist(lpcszOutputDir,&dwError))
	{
		if(!CreatePathname(lpcszOutputDir,&dwError))
		{
			printf("unable to create destination directory %s\n",lpcszOutputDir);
			return(1);
		}
		else
		{
			if(dwError!=ERROR_ALREADY_EXISTS)
				printf("directory created %s\n",lpcszOutputDir);
		}
	}

	// -r
	// copia ricorsiva
	BOOL bRecursiveyCopy = FALSE;
	if(opts[GETOPT_r].bFound)
		bRecursiveyCopy = TRUE;
    
	// -x<...>
	// esclusioni per la copia, per specificare piu' di un pattern, separarlo con ;
	// tenere a mente che se si esclude una dir (es. Debug), il programma riportera' solo 1 esclusione, pero'
	// di fatto tutti i file contenuti in essa non verranno presi in considerazione (*.obj, *.exe, etc.), quindi 
	// il totale reale delle esclusioni sara' maggiore
    if(opts[GETOPT_x].bFound)
	{
		if(!*opts[GETOPT_x].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_x].cOpt);
	        return(1);
		}
		else
		{
			pitemListExl = wildCards.SplitPattern(opts[GETOPT_x].uValue.szValue);
		}
	}
    
	// -q[dd/mm/yy|current]
	// solo copia i files modificati a partire dalla data specificata, nel formato dd/mm/yy
	// se la data viene omessa, assume quella corrente
	BOOL bQuickMode = FALSE;
    if(opts[GETOPT_q].bFound)
	{
		if(*opts[GETOPT_q].uValue.szValue)
		{
			int day,month,year;
			if(strcmp(opts[GETOPT_q].uValue.szValue,"current")==0)
			{
				day = dateTime.GetDay();
				month = dateTime.GetMonth();
				year = dateTime.GetYear();
			}
			else
			{
    			if(sscanf(opts[GETOPT_q].uValue.szValue,"%d/%d/%d",&day,&month,&year)==3)
				{
					if(year < 99)
						year += 2000;
					dateTime.SetDate(-1,day,month,year);
				}
				else
				{
					printf("error: wrong date format, assuming today (%d/%d/%d)\n",dateTime.GetDay(),dateTime.GetMonth(),dateTime.GetYear());
				}
			}
			bQuickMode = TRUE;
		}
		else
		{
			printf("error: you must specify a valid date for the copy\n");
		    return(1);
		}
	}
	else
		dateTime.SetDate(-1,26,8,1965);
		

	// -X
	// visualizza le esclusioni
	BOOL bShowExcluded = FALSE;
	if(opts[GETOPT_X].bFound)
		bShowExcluded = TRUE;

	// -f match filename
	if(opts[GETOPT_f].bFound)
		bMatchWhole = FALSE;
	// -F match path
	if(opts[GETOPT_F].bFound)
		bMatchWhole = TRUE;

	// -y<...>
	// esclusioni per l'api gzw, per specificare piu' di un pattern, separarlo con ;
	// se omesso, assume il valore specificato per -x
	if(opts[GETOPT_y].bFound)
	{
		if(!*opts[GETOPT_y].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_y].cOpt);
	        return(1);
		}
	}
	else
	{
		if(*opts[GETOPT_x].uValue.szValue)
			strcpyn(opts[GETOPT_y].uValue.szValue,opts[GETOPT_x].uValue.szValue,sizeof(opts[GETOPT_y].uValue.szValue));
	}

	// -v[...]
	// genera il file .gzw contenente i files copiati sopra, bisogna specificare il numero di versione
	char szVersion[32] = {0};
	if(opts[GETOPT_v].bFound)
	{
		if(!*opts[GETOPT_v].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_v].cOpt);
	        return(1);
		}
		else
			strcpyn(szVersion,opts[GETOPT_v].uValue.szValue,sizeof(szVersion));
    }

	// -V<...>
	// directories aggiuntive per versionamento, per specificare piu' di una, separarle con ;
    if(opts[GETOPT_V].bFound)
	{
		if(!*opts[GETOPT_V].uValue.szValue)
		{
			printf("error: the -%c option requires a valid argument, use -h for help\n",opts[GETOPT_V].cOpt);
			return(1);
		}
		else
			strcpyn(szDirectoriesToBeVersioned,opts[GETOPT_V].uValue.szValue,sizeof(szDirectoriesToBeVersioned));
	}

/*- COPIA ------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

    // effettua la copia
    int nCopiedFiles = 0;
    int nExcludedFiles = 0;
	int nUnchangedFiles = 0;

    printf("copying newer files from %s to %s\n",lpcszInputDir,lpcszOutputDir);
	if(bQuickMode)
	    printf("matching (>=) %d/%d/%d date\n",dateTime.GetDay(),dateTime.GetMonth(),dateTime.GetYear());
	printf("exclusions: ");
	if(pitemListExl)
	{
		int nTot = pitemListExl->Count();
		ITEM* pItem = NULL;
		int i = 0;
		if(nTot > 0)
		{
			ITERATOR iter = pitemListExl->First();
			while(iter!=(ITERATOR)NULL)
			{
				pItem = (ITEM*)(iter->data);
				if(pItem)
					printf("%s%s",pItem->item,i++ < nTot-1 ? ", " : "\n");
				iter = pitemListExl->Next(iter);
			}
		}
	}
	if(!pitemListExl || pitemListExl->Count() <= 0)
		printf("none\n");

	DWORD dwTot = 0L;
	DWORD dwRet = CopyDirectory(lpcszInputDir,
								lpcszOutputDir,
								bRecursiveyCopy,
								&wildCards,
								bMatchWhole,
								nCopiedFiles,
								nUnchangedFiles,
								nExcludedFiles,
								bShowExcluded,
								dateTime,
								dwTot);


	printf("\r%*s\rfiles/directories analized: %ld\n",GetConsoleWidth()-1," ",dwTot);
	printf("files copied: %d\n",nCopiedFiles);
	printf("unchanged files: %d\n",nUnchangedFiles);
	printf("excluded files/directories: %d\n",nExcludedFiles);

	if(dwRet!=NO_ERROR)
	{
		printf("found error(s) while copying, press any key to continue...");
		Beep(1000,300);
		getch();
		printf("\n");
	}

	char szInputDrive[5] = {0};
	char szOutputDrive[5] = {0};
	DISKINFO di = {0};
	if(GetDriveFromPath(opts[GETOPT_s].uValue.szValue,szInputDrive,sizeof(szInputDrive))==1)
	{
		if(GetDiskInfo(szInputDrive,&di))
		{
			char szDiskSize[32] = {0};
			char szFreeSpace[32] = {0};
			strsizefmt(szDiskSize,sizeof(szDiskSize),(double)di.totalBytes);
			strsizefmt(szFreeSpace,sizeof(szFreeSpace),(double)di.freeBytes);
			printf("%s of free space on drive %s (%s total capacity)\n",szFreeSpace,szInputDrive,szDiskSize);

			if(GetDriveFromPath(opts[GETOPT_d].uValue.szValue,szOutputDrive,sizeof(szOutputDrive))==1)
				if(strcmp(szInputDrive,szOutputDrive)!=0)
				{
					memset(&di,'\0',sizeof(di));
					if(GetDiskInfo(szOutputDrive,&di))
					{
						strsizefmt(szDiskSize,sizeof(szDiskSize),(double)di.totalBytes);
						strsizefmt(szFreeSpace,sizeof(szFreeSpace),(double)di.freeBytes);
						printf("%s of free space on drive %s (%s total capacity)\n",szFreeSpace,szOutputDrive,szDiskSize);
					}
				}
		}
	}

	// Ta Da!
	PlayEmbeddedWave(IDR_WAVE1);

/*- VERSIONAMENTO ----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

    // effettua il versionamento
	if(opts[GETOPT_v].bFound)
	{
		// versionamento:
		// in teoria, in seguito alla copia, tutti i files della dir sorgente sono presenti nella dir dst, pero' come input per 
		// il file .gzw usa la dir sorgente, NON la dst, per evitare di includere gli eventuali .gzw creati in precedenza (dato
		// che i .gzw vengono creati nella dir dst)
		// oltretutto, dato che nel .gzw viene memorizzato il pathname assoluto, deve salvare (memorizzare) il pathname originale,
		// non quello di destinazione della copia, che potrebbe essere uno qualsiasi senza nessuna relazione con la dir sorgente

/*- creazione .gzw per progetto da versionare ------------------------------------------------------------------------------------------------------------------------------------*/

		CGzw gzw;
		FPGZWCALLBACK pGzwCallback = GzwCallback;
		gzw.SetCallback(pGzwCallback);
		gzw.SetOverwrite(TRUE);
		gzw.SetOperation(GZW_COMPRESS);
		gzw.SetRecursive(TRUE);
		gzw.SetPathScheme(GZW_PATHSCHEME_RELATIVE);
		gzw.SetExcludePattern(!*opts[GETOPT_y].uValue.szValue ? NULL : opts[GETOPT_y].uValue.szValue,TRUE);
		gzw.SetPatternMatchWhole(TRUE);

		// costruisce lo skeleton di input per il gzw usando la directory sorgente + "*.*"
		char szInputFiles[_MAX_FILEPATH+1] = {0};
		snprintf(szInputFiles,sizeof(szInputFiles),"%s\\*.*",lpcszInputDir);
		gzw.SetInput(szInputFiles);

		// il file di output (compresso) lo genera nella directory di destinazione usando lo stesso nome della directory 
		// sorgente ed aggiungendo l'identificativo della versione, da specificare obbligatoriamente (opzione -v)
		char szOutputFile[_MAX_FILEPATH+1] = {0};
		char szGzwName[_MAX_FILEPATH+1] = {0};
		const char* p = strrchr(lpcszInputDir,'\\');
		if(p)
			p++;
		else
			p = lpcszInputDir;
		strcpyn(szGzwName,p,sizeof(szGzwName));
		snprintf(szOutputFile,sizeof(szOutputFile),"%s\\%s.%s.gzw",lpcszOutputDir,szGzwName,!*szVersion ? "0.0" : szVersion);
		gzw.SetOutput(szOutputFile);

		printf("\nversioning to %s\n",!*szVersion ? "0.0" : szVersion);
		printf("%s will be compressed into %s\n",szInputFiles,szOutputFile);
		if(*opts[GETOPT_y].uValue.szValue)
			printf("exclusions: %s\n",opts[GETOPT_y].uValue.szValue);

		// crea il .gzw contenente la copia compressa della directory copiata sopra
		UINT nRet = gzw.Gzw();
		printf("\ncompression ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);

		// dop aver creato il .gzw, produce la lista relativa
		char szListFilename[_MAX_FILEPATH+1] = {0};
		gzw.SetOperation(GZW_LIST);
		gzw.SetInput(szOutputFile);
		snprintf(szListFilename,sizeof(szListFilename),"%s\\%s.%s.txt",lpcszOutputDir,szGzwName,!*szVersion ? "0.0" : szVersion);
		gzw.SetOutput(szListFilename);
		printf("generating %s\n",szListFilename);
		gzw.Gzw();
		printf("listing ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);

/*- creazione .gzw x directory (relative al progetto) da versionare --------------------------------------------------------------------------------------------------------------*/

		// se esistono directories (progetti) da versionare
		if(*szDirectoriesToBeVersioned)
		{
			int len = 0;
			const char* delimiter = ";";
			char* token = strtok(szDirectoriesToBeVersioned,delimiter);
			char szVersionedFilename[_MAX_FILEPATH+1] = {0};

			// per ognuna delle directories da versionare
			while(token!=NULL)
			{
				// compressione dei files della dir nel .gzw relativo
				// normalizza il nome della directory, NON deve terminare con '\'
				strcpyn(szVersionedFilename,token,sizeof(szVersionedFilename));
					
				len = strlen(szVersionedFilename)-1;
				if(szVersionedFilename[len]=='\\')
					szVersionedFilename[len] = '\0';

				printf("\nalso versioning %s\n",szVersionedFilename);

				// costruisce lo skeleton di input per il gzw usando la directory + "*.*"
				snprintf(szInputFiles,sizeof(szInputFiles),"%s\\*.*",szVersionedFilename);
				gzw.SetInput(szInputFiles);
			
				// il file di output (compresso) lo crea nella directory di destinazione, usando il nome della directory
				// di destinazione + l'identificativo della versione
				char szGzwName[_MAX_FILEPATH+1] = {0};
				char* p = (char*)strrchr(szVersionedFilename,'\\');
				if(p)
					p++;
				else
					p = szVersionedFilename;
				strcpyn(szGzwName,p,sizeof(szGzwName));
				snprintf(szOutputFile,sizeof(szOutputFile),"%s\\%s.%s.gzw",lpcszOutputDir,szGzwName,!*szVersion ? "0.0" : szVersion);
				gzw.SetOutput(szOutputFile);

				// comprime i files della directory da versionare creando il .gzw relativo
				printf("%s will be compressed into %s\n",szInputFiles,szOutputFile);
				gzw.SetOperation(GZW_COMPRESS);
				gzw.Gzw();
				printf("\ncompression ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);

				// dop aver creato il .gzw, produce la lista relativa
				gzw.SetInput(szOutputFile);
				snprintf(szListFilename,sizeof(szListFilename),"%s\\%s.%s.txt",lpcszOutputDir,szGzwName,!*szVersion ? "0.0" : szVersion);
				printf("generating %s\n",szListFilename);
				gzw.SetOutput(szListFilename);
				gzw.SetOperation(GZW_LIST);
				gzw.Gzw();
				printf("listing ended %s (%ld)\n",nRet==GZW_SUCCESS ? "successfully" : "with errors",nRet);

				token = strtok(NULL,delimiter);
			}
		}

		printf("\nversioning concluded %s\n",nRet==GZW_SUCCESS ? "successfully" : "with errors");
	}

    return(0);
}

/*
	CopyDirectory()

	Copia (ricorsivamente) la directory sorgente su quella di destinazione.
*/
DWORD CopyDirectory(LPCSTR		lpcszSrcDir,
					LPCSTR		lpcsDstDir,
					BOOL		bRecursiveyCopy,
					CWildCards* pWildCards,
					BOOL		bMatchWhole,
					int&		nCopiedFiles,
					int&		nUnchangedFiles,
					int&		nExcludedFiles,
					BOOL		bShowExcluded,
					CDateTime&	dateTime,
					DWORD&		dwTot
					)
{
	// le static qui sotto perche' la funzione e' ricorsiva

	// per -q da cmd, solo controlla se la data sorgente >= data specificata, non contro destinazione
	static TERN tQuickMode = Undef;
	static SYSTEMTIME systemTime = {0};
	static FILETIME fileTime = {0};
	if(tQuickMode==Undef)
	{
		tQuickMode = (dateTime.GetDay()==26 && dateTime.GetMonth()==8 && dateTime.GetYear()==1965) ? False : True;
		if(tQuickMode==True)
		{
			systemTime.wDay = dateTime.GetDay();
			systemTime.wMonth = dateTime.GetMonth();
			systemTime.wYear = dateTime.GetYear();
		}
		SystemTimeToFileTime(&systemTime,&fileTime);
	}

	// per tenere traccia di eventuali errori
	static DWORD dwRet = NO_ERROR;

	// per tenere traccia del totale dei files
	static long nTotFiles = 0L;

	// per tenere traccia di quanti caratteri stampati su stdout e ripulire i caratteri 
	// spuri della visualizzazione precedente, dato che piu' sotto usa printf con \r
	static int wide = 0;

	// inizia cercando qualsiasi file, piu' sotto esclude i patterns specificati e confronta per data/ora
	char szSearchPattern[MAX_PATH+1] = {0};
	snprintf(szSearchPattern,sizeof(szSearchPattern),"%s\\*.*",lpcszSrcDir);

	DWORD dwError = 0L;
	BOOL bMatch = FALSE;
    WIN32_FIND_DATAA findData = {0};
    HANDLE hFind = ::FindFirstFile(szSearchPattern,&findData);
	if(hFind==INVALID_HANDLE_VALUE)
	{
		dwRet = ERROR_FILE_NOT_FOUND;
		SetLastError(dwRet);
		return(dwRet);
	}

    do {
		// salte le dir "." e ".."
		if(strcmp(findData.cFileName,".")==0 || strcmp(findData.cFileName,"..")==0)
			continue;

        char szSrcPath[MAX_PATH+1];
		char szDstPath[MAX_PATH+1];
        snprintf(szSrcPath,sizeof(szSrcPath),"%s\\%s",lpcszSrcDir,findData.cFileName);
        snprintf(szDstPath,sizeof(szDstPath),"%s\\%s",lpcsDstDir,findData.cFileName);

		printf("\r%*s\r",wide+1," ");
		wide = printf("\rfiles/directories analized: %ld (%s)",++nTotFiles,szSrcPath);

		// confronta ogni file/path via CWildCards
		// a seconda del flag (-f/-F), verifica il match solo sul nome dell'oggetto (che puo' essere un file o una directory) o sull'intero pathname
		// se e' una directory, automaticamente include o esclude tutti i files in essa contenuti
		if(bMatchWhole)
			bMatch = DoesMatchWithPatterns(szSrcPath,pWildCards);
		else
			bMatch = DoesMatchWithPatterns(findData.cFileName,pWildCards);

		if(bMatch)
		{
			if(bShowExcluded)
			{
				printf("\r%*s\r",wide+1," ");
				wide = printf("\rexcluded: %s\n",szSrcPath);
			}
			nExcludedFiles++;
			continue;
        }

		// e' una directory, crea e copia ricorsivamente...
        if(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// ...solo se e' stato specificata l'opzione -r
			if(bRecursiveyCopy)
			{
				// verifica la directory di destinazione
				if(!DoesDirectoryExist(szDstPath,&dwError))
				{
					if(!CreatePathname(szDstPath,&dwError))
					{
						printf("\r%*s\r",wide+1," ");
						wide = printf("\rerror: unable to create %s\n",szDstPath);

						dwRet = ERROR_ACCESS_DENIED;
						SetLastError(dwRet);
						return(dwRet);
					}
					else
					{
						if(dwError!=ERROR_ALREADY_EXISTS)
						{
							printf("\r%*s\r",wide+1," ");
							wide = printf("\rdirectory created %s\n",szDstPath);
						}
					}
				}

				CopyDirectory(szSrcPath,szDstPath,bRecursiveyCopy,pWildCards,bMatchWhole,nCopiedFiles,nUnchangedFiles,nExcludedFiles,bShowExcluded,dateTime,dwTot);
			}
        }
		else // e' un file, copia solo se e' piu' recente o se non esiste
		{
			BOOL bDoCopyFile= FALSE;
			if(tQuickMode==True)
				bDoCopyFile = CompareFilebyDate(szSrcPath,&fileTime);		// confronta la data del file (sorgente) con quella specificata in input via -q
			else
				bDoCopyFile = CompareFileTimebyName(szSrcPath,szDstPath);	// confronta la data/ora del file (sorgente) con quella del file di destinazione

			if(bDoCopyFile)
			{
				//if(CopyFile(szSrcPath,szDstPath,FALSE))							// lento
				if((dwError = CopyFileMapped(szSrcPath,szDstPath))==ERROR_SUCCESS)	// rapido
				{
					printf("\r%*s\r",wide+1," ");
					wide = printf("\rcopied: %s\n",szDstPath);
                    nCopiedFiles++;
                }
				else // copia fallita
				{
					if((dwRet = dwError)==ERROR_HANDLE_EOF) // CopyFileMapped() restituisce EOF per file a dimensione 0
					{
						printf("\r%*s\r",wide+1," ");
						wide = printf("\rwarning: file size is zero: %s\n",szSrcPath);

						if((dwError = CopyFile(szSrcPath,szDstPath,FALSE))==0L) // copia il file a dimensione 0
						{
							dwRet = GetLastError();
							printf("\r%*s\r",wide+1," ");
							wide = printf("\rerror while copying: %s (%lu)\n",szSrcPath,dwRet);
						}
						else
							dwRet = NO_ERROR;
					}
					else
					{
						printf("\r%*s\r",wide+1," ");
						wide = printf("\rerror while copying: %s (%lu)\n",szSrcPath,dwRet);
					}
                }
			}
			else
			{
				nUnchangedFiles++;
			}
		}

		// evita il sovraccarico del sistema
		::Yield();

    } while(!g_bInterrupted && ::FindNextFile(hFind,&findData));

    FindClose(hFind);

	dwTot = nTotFiles;

	SetLastError(dwRet);
	return(dwRet);
}

/*
	DoesMatchWithPatterns()

	Confronta lo skeleton/wildcard presente nella lista ('pItem->item') con la stringa ricevuta come
	parametro ('name') verificando se esiste o meno il match.
	A seconda del flag usato da linea di comando (-f o -F), esegue la verifica solo sul nome file o
	sull'interno pathname.
	Per il confronto usa Match()/MatchSubString() a seconda se l'elemento della lista ('pItem->item') 
	contiene uno skeleton o una wildcard.
	Versione alternativa a DoesFileNameMatchWithVectorOfPatterns().
*/
bool DoesMatchWithPatterns(const char* name,CWildCards* pWildCards)
{
	bool match = false;
	CItemList* pitemList = pWildCards->GetItemList();

	if(pitemList)
	{
		int nTot = pitemList->Count();
		ITEM* pItem = NULL;
		if(nTot > 0)
		{
			ITERATOR iter = pitemList->First();
			while(iter!=(ITERATOR)NULL)
			{
				match = false;

				pItem = (ITEM*)(iter->data);
				if(pItem)
				{
					if(pItem->flag==1)
					{
						if(!match)
							match = pWildCards->Match(pItem->item,name);			// SI puo' contenere caratteri jolly
					}
					else if(pItem->flag==0)
					{
						if(!match)
							match = pWildCards->MatchSubString(pItem->item,name);	// NON puo' contenere caratteri jolly, farebbe fallire strstr()
					}
				}

				if(match)
					break;
				
				iter = pitemList->Next(iter);
			}
		}
	}

    return(match);
}

/*
	PlayEmbeddedWave()

	Riproduce il .wav, estraendolo dalle risorse.
*/
void PlayEmbeddedWave(UINT nResourceID)
{
	// IDR_WAVE1 scaricato da soundbible.com
	// dall'explorer del progetto principale aprire l'explorer del file .rc, right click sul file .rc e aggiungere risorsa...
	PlaySound(	MAKEINTRESOURCE(nResourceID),
				GetModuleHandle(NULL),
				SND_RESOURCE | SND_SYNC | SND_NODEFAULT
				);
}

/*
    CtrlHandler()

	Gestore interruzioni.
	Il meccanismo per non scasinare durante le operazioni di I/O e' il seguente:
	- qui, nel gestore, per ogni interruzione intercettata chiama la callback con il valore GZW_CALLBACK_HALT
	- la callback, a partire da quando viene chiamata con GZW_CALLBACK_HALT, restituisce sempre 0
	- chi chiama la callback, al ricevere 0 come risultato, interrompe l'elaborazione corrente
	- l'elaborazione viene interrotta a livello API (GZW), lasciando terminare le funzioni in corso della zLib
	  (che non effettuano nessun check), in modo tale che queste ultime possano terminare l'operazione di I/O 
	  sul file corrente
*/
BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
{
    switch(dwCtrlType)
	{
        case CTRL_C_EVENT:
            printf("\nCtrl+C detected, ending program...\n");
			g_bInterrupted = TRUE;
            return(TRUE);

        case CTRL_BREAK_EVENT:
            printf("\nCtrl+Break detected, ending program...\n");
			g_bInterrupted = TRUE;
            return(TRUE);

        case CTRL_CLOSE_EVENT:
            printf("\nConsole closing, ending program...\n");
			g_bInterrupted = TRUE;
            return(TRUE);

        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            printf("\nShutting down, ending program...\n");
			g_bInterrupted = TRUE;
            return(TRUE);
        
        default:
            return(FALSE);			// passa la gestione al SO
			//return(TRUE);			// gestisce in proprio
    }
}
