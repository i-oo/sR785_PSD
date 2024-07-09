/*OnyeBUCHI Israel Onumonu

=>	Copy libftd2xx.a, ftd2xx.h, Makefile & WinTypes.h
=>	to the same folder as source code then compile

	gcc -o RunPSD RunPSD.c -L. -lftd2xx -Wl,-rpath /usr/local/lib

DWORD = ULONG = unsigned long
DWORD =	*FT_HANDLE;
ULONG =	FT_STATUS;
LPVOID = void*
*/

#include <stdio.h>			//for input output stream manipulation
#include <string.h>			//for strings and character array
#include <stdlib.h>			//for general purpose C functions
#include <math.h>			//for various mathematical functions
#include <sys/stat.h>		//to get file attributes
#include <fcntl.h>			//to create file
#include <errno.h>			//to get system error
#include <termios.h> 		// POSIX terminal control definitions
#include <unistd.h>  		// UNIX standard function definitions
#include <time.h>			//to get system time
#include "ftd2xx.h"			//prologix header file

#define SR785 10			// default GPIB address for SR785

int baudRate = 9600;		//global Baud Rate
char recv[256];				//global GPIB receive string
char cmd[64];				//global GPIB command string
char temp[256];				//global temporary string
char buffer[32];			//RS232 receive string
DWORD length;

/********************************************************************************************/
/************************************SUBROUTINE INITIALIZATION*******************************/
/********************************************************************************************/
void DataDump(FT_HANDLE handle, int fDesc, float offset, float freqStep, float strtFreq);			//Make SR785 Dump data in Display
//void Shift(FT_HANDLE handle, float strtFreq, float offset, int baseFreq, int fDesc, float freqStep);//Make SR785 FREQUENCY SHIFT till 102.4kHz/100kHz
BOOLEAN TxGpib(FT_HANDLE handle, const char* buffer);	//WRITE TO GPIB
BOOLEAN RxGpib(FT_HANDLE handle);						//READ FROM GPIB
BOOLEAN WaitIFC(FT_HANDLE handle);						//Serial Poll until IFC(bit7) is set(command done)
void TxDevice(FT_HANDLE handle, const char* buffer);	//WRITE TO Device
void RxDevice(FT_HANDLE handle, const char* buffer);	//Read From Device-----for query commands
void WaitAvg(FT_HANDLE handle);							//Routine to Start a Linear Avg and wait until both Displays are done
double GetData(FT_HANDLE handle, int disp, int bin);	//Routine to Move the Display Marker to a bin and Return the Data Value
int File(char mType[]);						//Creates/Opens a Text File and returns its object handle
FT_HANDLE Init(void);									//Initialize GPIB Link
void SpectrumAn(FT_HANDLE handle, int fDesc);			//SR785 - RUN NOISE MEASUREMENT PSD


//end of initialization

/*---------------------------Starting Point------------------------------------------------*/
int main(int argc, char* argv[])
{
/********************************************************************************************/
/**************************CHECK INPUT PARAMETER FOR VALID FORMAT****************************/
/********************************************************************************************/

//argv[0] is the address of the program


	if(argc !=3)
    	{
        	printf("\nUsage: \nsudo ./RunPSD <Filename> <Comment> \n\n NO SPACES IN FILENAME OR COMMENT \n\n Example: <VO2_100Gain_1e-6AVGain_0.5V_25oC_200Hz> <High_Vacuum or Optical_Excitation or No_Comment>\n\n");
        	return 0;
    	}


	char mType[64];						//Material_Name e.g. GeTe15Bi for Germanium Teluride 10E15 Bismuth doped
	sprintf(mType,"%s", argv[1]);

	char comment[256];					//Comment
	sprintf(comment,"%s", argv[2]);


//CHECK INPUT PARAMETER VALIDITY 2
	char digits[] = "0123456789";
	char others[] = "~!@#$%^&*()/\\+=<>?|}{][`;\"";
	char csv[] = ",";
	char letter[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	char bash[] = "!&()<>|`\"'";


	//Check Material Name for right format
	char *mType_ptr0 = strpbrk(mType, others);		//check if it contains others
	char *mType_ptr1 = strpbrk(mType, csv);			//check if it contains csv
	char *mType_ptr2 = strpbrk(mType, letter);		//check if it contains letters
	if (mType_ptr0 != NULL || mType_ptr1 != NULL || mType_ptr2 == NULL) 	//if it contains others[] and csv[]
	{
		printf("\nOnly letters, numbers,hyphen, period and underscore are allowed for <FileName>\n\n");
		return 0;
	}



/********************************************************************************************/
/****************************************INITIALIZE CONNECTION*******************************/
/********************************************************************************************/
	system("echo 3c19.1 | sudo -S rmmod ftdi_sio");	//unmount ftdi module. Replace 3c19.1 with new password. This is important for first run after boot up.


	FT_HANDLE handle = Init();

	if(handle == 0){return 0;}

    // Controller mode
    TxGpib(handle, "++mode 1\n");

    // No automatic read-after-write
    TxGpib(handle, "++auto 0\n");		//instrument to listen

    // No GPIB termination characters(LF,CF,ESC,+) appended
    TxGpib(handle, "++eos 3\n");

    // Assert EOI with last byte
    TxGpib(handle, "++eoi 1\n");

/********************************************************************************************/
/********************************************TALK TO DEVICES**********************************/
/********************************************************************************************/

//Create File 
	int fDesc = File(mType);
	char input[256];	

//Write Comment to file
	sprintf(temp, "\n#Comment\n#");
	write(fDesc, temp, strlen(temp));		
	write(fDesc, comment, strlen(comment));			//actual comment
	sprintf(temp, "\n");
	write(fDesc, temp, strlen(temp));		


	SpectrumAn(handle, fDesc);				//RUN NOISE MEASUREMENT PSD


	close(fDesc);						// close Prologix file descriptor
	FT_Close(handle);					// close link between Gpib controller and Device


	printf("\a\a\a\a\a");

    return 0;
}//end of main




/***************************************************************************/
/****************************SUBROUTINES************************************/
/***************************************************************************/


/*------------------------SR785 - RUN NOISE MEASUREMENT PSD--------------------------------------------------*/
void SpectrumAn(FT_HANDLE handle, int fDesc)
{
    // GPIB address of SR785
    sprintf(cmd, "++addr %d\n", SR785);
    TxGpib(handle, cmd);

/*
	TxDevice(handle, "*CLS"); 			//clear all status words
	TxDevice(handle, "OUTX0"); 			//direct SR785 responses to GPIB interface

	// ****** Set the Standard Event enable register to catch EXE and CME
	// Command errors will set the ESB bit in the Serial Poll status word
	TxDevice(handle, "*ESE 48"); 		//set bits 4 and 5
*/

	//Read previous data from SR785 Transmit Queue
	RxGpib(handle);						//remove sticky bit from comm channel

//	TxDevice(handle, "*CLS"); 			//clear all status words

	//TxDevice(handle, "I1RG -6");		//Set Input range

//	TxDevice(handle, "A1RG 1");			//Auto range ON

//	TxDevice(handle, "A1RG 0");			//Auto range OFF

	RxDevice(handle, "UNIT? 0");		//Query Measurement Unit
	sprintf(temp, "\n#Measurement Unit: ", recv);
	write(fDesc,temp,strlen(temp));
	int n;
	for(n = 0; n < strlen(recv); n++)
	{
		if(recv[n] == '\n')
		{break;}
		else
		{
			if((int)recv[n] == -3)
			{
				sprintf(temp,"^2");
				write(fDesc,temp,strlen(temp));
			}
			else
			{
				sprintf(temp,"%c",recv[n]);
				write(fDesc,temp,strlen(temp));
			}
		}
	

	}
	sprintf(temp,"\n");	
	write(fDesc,temp,strlen(temp));	
	

//Query Frequency parameter and write to file
	int fbas[] = {100000, 102400};
	int flin[] = {100,200,400,800};
	int baseFreq;
	float offset, freqStep, strtFreq;

//	RxDevice(handle, "FBAS? 0");		//Query Base Frequency
//	sprintf(temp, "Base Frequency: %d Hz\n", fbas[atoi(recv)]);
	baseFreq = fbas[1];//fbas[atoi(recv)];
//	write(fDesc,temp,strlen(temp));		//write Base Frequency to file	

	RxDevice(handle, "FSPN? 0");		//Query Frequency Span
	sprintf(temp, "#Frequency Span: %f Hz\n", atof(recv));
	offset = atof(recv);
	write(fDesc,temp,strlen(temp));		//write Frequency Span to file	

//	RxDevice(handle, "FLIN? 0");		//Query Resolution
	sprintf(temp, "#Resolution (Number of bins per display): 800\n");//, flin[atoi(recv)]);
	freqStep = offset/800; //flin[atoi(recv)];	//definition of frequency step/resolution
	write(fDesc,temp,strlen(temp));		//write Resolution to file	

	strtFreq = 0;
	

//	WaitAvg(handle);					//Start measurement and wait until Display A is through averaging
	DataDump(handle, fDesc, offset, freqStep, strtFreq);	//Dump data in Display A
}//end of SR785()


/*------------------------SR785 - PERFORM FREQUENCY SHIFT TILL 102.4kHz--------------------------------------------------
void Shift(FT_HANDLE handle, float strtFreq, float offset, int baseFreq, int fDesc, float freqStep)
{
	int n = baseFreq/offset;
	
	while(strtFreq <= offset*(n-1))
	{
		TxGpib(handle, "ASCL 0;"); 	//FOR CONVENIENCE autoscale the displays
		sprintf(cmd, "FSTR 0,%d", strtFreq);//Set Start Frequency
		TxDevice(handle, cmd);
		WaitAvg(handle);			//Start measurement and wait until Display A is through averaging

		DataDump(handle, fDesc, offset, freqStep, strtFreq);//Get data to dump

		strtFreq += offset;			//changing Start Frequency
	}
}//end of Shift()*


/*------------------------MAKE SR785 DUMP DATA IN DISPLAY--------------------------------------------------*/
void DataDump(FT_HANDLE handle, int fDesc, float offset, float freqStep, float strtFreq)
{
	/* ***************************************************** */
	/* ****** Binary transfer all of DisplayA spectrum ******/
	//We need to send the DSPB?0 command WITHOUT waiting for
	//IFC in serial poll status since IFC will not be set until
	//AFTER the transfer is complete!
	//This section needs to be modified for your GPIB interface
	
	float rxBuff[1024];								//size becomes 101,201,401 or 801
	
	printf("\nReading entire DisplayA...");
	TxGpib(handle, "DSPB? 0\n");					//DSPB?0 use TxGpib(don't wait for IFC)
	TxGpib(handle, "++auto 1\n");					//make the PC listen, SR785 talk
	FT_Read(handle, &rxBuff,(DWORD)4096, &length);	//read bin data 1024*4=4096
	WaitIFC(handle);								//serial poll until IFC set, ok to continue
	printf("%d bytes read\n", length);				//actual number of bytes read

	int rxBuffSize = sizeof(rxBuff)/sizeof(rxBuff[0]);
	int i;

	//Data Heading
	sprintf(temp, "\n#Freq\tData\n");
	write(fDesc, temp, strlen(temp));

	//Data Value
	for(i=0; i<rxBuffSize; i++) 
	{
	printf("%f\t%e\n", i, ((i*freqStep)+strtFreq), rxBuff[i]);

	sprintf(temp, "%f\t%e\n", i, ((i*freqStep)+strtFreq), rxBuff[i]);
	write(fDesc, temp, strlen(temp));

	if(rxBuff[i+1] == 0.000000000000000000000000000000000) break;	//exit when data is 0.000000 (end of data)
	}

	TxGpib(handle, "++auto 0\n");					//make the SR785 listen

}//end of DataDump()


/*------------------------WRITE TO GPIB--------------------------------------------------*/
BOOLEAN TxGpib(FT_HANDLE handle, const char* buffer)
{
    size_t len = strlen(buffer);
	unsigned long bytesWritten;

    FT_STATUS status = FT_Write(handle,(LPVOID)buffer,(DWORD)len, &bytesWritten);

    if((status != FT_OK))//  ||(bytesWritten != len))	//ocassionally, !=len returns false when things are good
        {
        printf("\nUnable to write '%s' to controller. Status: %d\r\n", buffer, status);
        return FALSE;
        }

	printf("\nWrote '%s' to controller. Status: %d\r\n", buffer, status);

	return TRUE;
}//end of TxGpib()


/*------------------------READ FROM GPIB--------------------------------------------------*/
BOOLEAN RxGpib(FT_HANDLE handle)
{ 
	DWORD RxBytes = 0;
	char resp[256];				//response string
	unsigned long bytesRead;	//bytes read

	TxGpib(handle, "++read eoi\n");

    FT_STATUS status = FT_Read(handle, &resp, sizeof(resp), &bytesRead);
	if((status != FT_OK))
    {
		printf("\nUnable to read from controller. Status: %d\r\n", status);
		return FALSE;
    }

	strcpy(recv, resp);
	return TRUE;
}//end of RxGpib()


/*---------------Serial Poll until IFC(bit7) is set(command done)-----------*/
BOOLEAN WaitIFC(FT_HANDLE handle)
{
	char stb;				//stb is serial poll byte
	char resp[50];
	unsigned long bytesRead;
	unsigned long bytesWritten;

	do{
		FT_Write(handle,(LPVOID)"\n",(DWORD)strlen("\n"), &bytesWritten);

		FT_Write(handle,(LPVOID)"++spoll\n",(DWORD)strlen("++spoll\n"), &bytesWritten);	

		FT_Write(handle,(LPVOID)"++status\n",(DWORD)strlen("++status\n"), &bytesWritten);	

		FT_Read(handle, &resp, sizeof(resp), &bytesRead);

		stb = atoi(resp);
	}while(!(stb & 128));	//IFC bit 0b10000000
	
	if(stb & 32) {		//0b100000
		// If ESB bit(bit5) is set,
		// there must be a command error in the Standard Event status word.
		// Handle command errors here.
		TxGpib(handle, "*ESR?\n"); //clear the Standard Event status word
		RxGpib(handle);
		printf("\nEXE error\n");
		return FALSE;
	}
	return TRUE;
}//end of WaitIFC()


/*------------------------WRITE TO Device--------------------------------------------------*/
void TxDevice(FT_HANDLE handle, const char* buffer)
{
	//WaitIFC(handle);		//serial poll until IFC set, ok to continue
	TxGpib(handle, buffer);
	WaitIFC(handle);		//serial poll until IFC set, ok to continue

}//end of TxDevice()


/*------------------------Read From Device-----for query commands-----------------------------------*/
void RxDevice(FT_HANDLE handle, const char* buffer)
{
	TxDevice(handle, buffer);
	RxGpib(handle);
}//end of RxDevice()


/*-------------------Routine to Start a Linear Avg and wait until Displays averaging is done---------------------
void WaitAvg(FT_HANDLE handle)
{
	int result, avgdone = 0;				//init avgdone status
	
	//RxDevice(handle, "DSPS?"); 			//clear sticky bits in Display status word first
	TxGpib(handle, "STRT;");
	//start lin average measurement
	do {
		RxDevice(handle, "DSPS? 1");		//check if Display A is through averaging
		result=atoi(recv); 					//read display status word

	}while(result != 1);					//while not set
	
}//end of WaitAvg()*/

	
/*------------Routine to Move the Display Marker to a bin and Return the Data Value---------*/
//move the marker in display disp to bin
double GetData(FT_HANDLE handle, int disp, int bin)
{
	sprintf(cmd,"MBIN %d,%d",disp,bin);
	TxDevice(handle, cmd);
	sprintf(cmd,"DSPY? %d,%d",disp,bin); 	//read the data value at bin
	RxDevice(handle, cmd);
	
	return(atof(recv)); 					//return the value as a double
}//end of GetData()


/*------------Creates/Opens a Text File and returns its object handle---------*/
//don't forget to use close(fdesc)when through writing to file
int File(char mType[])	//Material Type e.g. GeTe15Bi for Germanium Teluride 10E15 Bismuth doped
{
	//get and print time
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	char _time[64];
	char fName[144];


	

	sprintf(fName, "./Readings/%s.txt", mType);

	//create editable file in subdirectory
	int fDesc;	//file object handle

	fDesc=open(fName, O_CREAT|O_APPEND|O_WRONLY,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if(fDesc==0 || fDesc==-1)  {
		switch(errno) {
		    case EACCES:
		            fprintf(stderr,"EACCES\n");
		            break;
		    case EEXIST:
		            fprintf(stderr,"EEXIST\n");
		            break;
		    case EINVAL:
		            fprintf(stderr,"EINVAL\n");
		            break;
		    case EMFILE:
		            fprintf(stderr,"EMFILE\n");
		            break;
		    case ENOENT:
		            fprintf(stderr,"ENOENT\n");
		            break;
		    default:
		            fprintf(stderr,"%i\n",errno);
		}
	}
	//write time to file
	sprintf(_time, "\n\n#****************************");
	write(fDesc, _time, strlen(_time));
	sprintf(_time, "\n#Delimiter (tab and comma)\n#%d-%d-%d_%d:%d:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	write(fDesc, _time, strlen(_time));	
	sprintf(_time, "#****************************\n");
	write(fDesc, _time, strlen(_time));
	

	return fDesc;	
}//end of File()


/*--------------------------Initialize GPIB Link------------------------------------------------*/
FT_HANDLE Init(void)
{
    FT_HANDLE handle = 0;
	FT_STATUS status;
	FT_DEVICE_LIST_INFO_NODE *devInfo;
	DWORD numDevs;

    printf("\n\nConnecting to Prologix GPIB-USB Controller...\n\n");
    status = FT_Open(0, &handle);
    if(status != FT_OK)
        {
		printf("FT_Open(%d) failed, with error %d.\n", 0,(int)status);
		printf("Use 'lsmod | grep ModuleName' to check if ftdi_sio(and usbserial) modules are present.\n");
		printf("If so, unload them using 'sudo rmmod ModuleName', as they conflict with ftd2xx.\n\n");
        return 0;
        }

    printf("Successfully connected.\r\n");

	status = FT_ResetDevice(handle);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_ResetDevice returned %d.\n",(int)status);
	}
	
	status = FT_Purge(handle, FT_PURGE_RX | FT_PURGE_TX); // Purge both Rx and Tx buffers
	if(status != FT_OK) 
	{
		printf("Failure.  FT_Purge returned %d.\n",(int)status);
	}

	status = FT_SetBaudRate(handle,(ULONG)baudRate);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetBaudRate(%d) returned %d.\n", 
		       baudRate,
		      (int)status);
	}
	
	status = FT_SetDataCharacteristics(handle, 
	                                     FT_BITS_8,
	                                     FT_STOP_BITS_1,
	                                     FT_PARITY_NONE);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetDataCharacteristics returned %d.\n",(int)status);
	}
	                          
	// Indicate our presence to remote computer
	status = FT_SetDtr(handle);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetDtr returned %d.\n",(int)status);
	}

	// Flow control is needed for higher baud rates
	status = FT_SetFlowControl(handle, FT_FLOW_RTS_CTS, 0, 0);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetFlowControl returned %d.\n",(int)status);
	}

	// Assert Request-To-Send to prepare remote computer
	status = FT_SetRts(handle);
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetRts returned %d.\n",(int)status);
	}

	status = FT_SetTimeouts(handle, 7500, 7500);	// 7.5 seconds
	if(status != FT_OK) 
	{
		printf("Failure.  FT_SetTimeouts returned %d\n",(int)status);
	}

	return handle;
}//end of Init()


