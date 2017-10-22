//
//	IDC File to disassemble Sega Genesis/Megadrive rom
//	by Kaneda
//
//	Useage:
//	  launch IDA with "idag -a -p68000 -Smida.idc"
//	  Select your .bin file
//	  Press OK to the 2 dialog boxes following
//
//	0.1 (12 Nov 2004 ): Initial release
//  0.2 (01 Jun 2005 ): Support for start adress <0x200 (skip header)
//
//	Update on http://www.consoledev.fr.st
//

#include <idc.idc>

//-------------------------------------------------------------------------
static CW(off,name,cmt) {
  auto x;
  x = off;
  MakeWord(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

//-------------------------------------------------------------------------
static CD(off,name,cmt) {
  auto x;
  x = off;
  MakeDword(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

//-------------------------------------------------------------------------
static CB(off,name,cmt) {
  auto x;
  x = off;
  MakeByte(x);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

static CS(off,end,name,cmt) {
  auto x;
  x = off;
  MakeStr(x, end);
  MakeName(x,name);
  MakeRptCmt(x,cmt);
}

static mdVector(  ) {
auto i, addr;

CD(0x00, "initStack", "Initial Stack");
CD(0x04, "startAddress", "Start Address");
CD(0x08, "", "Bus Error");
CD(0x0C, "", "Address Error");
CD(0x10, "", "Illegal instruction");
CD(0x14, "", "Zero Divide");
CD(0x18, "", "CHK instruction");
CD(0x1C, "", "TRAPV instruction");
CD(0x20, "", "Privilege Violation");
CD(0x24, "", "Trace");
CD(0x28, "", "Line 1010 Emulator");
CD(0x2C, "", "Line 1111 Emulator");
CD(0x30, "", "Reserved");
CD(0x34, "", "Reserved");
CD(0x38, "", "Reserved");
CD(0x3C, "", "Unitialized Interrrupt");
CD(0x40, "", "Reserved");
CD(0x44, "", "Reserved");
CD(0x48, "", "Reserved");
CD(0x4C, "", "Reserved");
CD(0x50, "", "Reserved");
CD(0x54, "", "Reserved");
CD(0x58, "", "Reserved");
CD(0x5C, "", "Reserved");
CD(0x60, "", "Spurious Interrupt");
CD(0x64, "", "Level 1 interrupt");
CD(0x68, "", "Level 2/External  interrupt");
CD(0x6C, "", "Level 3 interrupt");
CD(0x70, "", "Level 4/Horizontal interrupt");
CD(0x74, "", "Level 5 interrupt");
CD(0x78, "", "Level 6/Vertical interrupt");
CD(0x7C,"", "Level 7 interrupt");

i=0x80;
while (i <= 0xBC){
  CD(i,"", "Trap");
  i = i+1;
}

i=0xC0;
while (i <= 0xFF){
  CD(i,"", "Reserved");
  i = i+1;
  }

for ( i=0x08; i< 0x200; i=i+4 ) {
  addr = Dword( i );
  MakeCode(addr);
  }
}

static mdHeader( ) {
auto addr;

addr =  Dword( 0x04 );
if (addr < 0x200)
{
	Warning("Start address unusual");
    return;
}
CS(0x100,0x110,"","");
 CS(0x110,0x120,"","");
 CS(0x120,0x130,"","");
 CS(0x130,0x140,"","");
 CS(0x140,0x150,"","");
 CS(0x150,0x160,"","");
 CS(0x160,0x170,"","");
 CS(0x170,0x180,"","");
 CS(0x180,0x18D,"","Serial Number");
 CW(0x18E, "CheckSum","");
 CS(0x190,0x1A0,"","");
 CD(0x1A0, "RomStartAdr", "Rom Start Adress");
 CD(0x1A4, "RomEndAdr", "Rom End Adress");
 CD(0x1A8, "RamStartAdr", "Ram Start Adress");
 CD(0x1AC, "RamEndAdr", "Ram End Adress");
 CS(0x1B0, 0x1BC, "", "SRam data");
 CS(0x1BC, 0x1C8, "", "Modem data");
 CS(0x1C8, 0x1DC, "", "Memo");
 CS(0x1DC, 0x1F0, "", "");
 CS(0x1F0, 0x200, "Country", "Countries codes");
 }
 
static mdAddress( ){
 CD(0xC00000, "VDP_Data","");
 CD(0xC00004, "VDP_Control",""); 
 CD(0xC00008, "HV_Counter",""); 
 CB(0xC00011, "PSG",""); 

 CW(0xA10001, "HW_Info","7-MODE  (R)  0: Domestic Model\n"
		        "             1: Overseas Model\n"
		        "6-VMOD  (R)  0: NTSC CPU clock 7.67 MHz\n"
                        "             1: PAL CPU clock 7.60 MHz\n"
                        "5-DISK  (R)  0: FDD unit connected\n"
		        "             1: FDD unit not connected\n"
		        "4-RSV   (R)  Currently not used\n"
		        "3-0 VER (R)  MEGA DRIVE version ($0 to $F)");

 CW(0xA10003,"DATA1",	"PD7 (RW)\n"
           		"PD6 (RW) TH\n"
           		"PD5 (RW) TR\n"
           		"PD4 (RW) TL\n"
           		"PD3 (RW) RIGHT\n"
           		"PD2 (RW) LEFT\n"
           		"PD1 (RW) DOWN\n"
           		"PDO (RW) UP\n" );
 CW(0xA10005,"DATA2","");
 CW(0xA10007,"DATA3","");
 CW(0xA10009,"CTRL1",	"INT (RW) 0: TH-INT PROHIBITED\n"
                     	"         1: TH-INT ALLOWED\n"
		     	"PC6 (RW) 0: PD6 INPUT MODE\n"
                     	"         1: OUTPUT MODE\n"
			"PC5 (RW) 0: PD5 INPUT MODE\n"
			"         1: OUTPUT MODE\n"
			"PC4 (RW) 0: PD4 INPUT MODE\n"
			"         1: OUTPUT MODE\n"
			"PC3 (RW) 0: PD3 INPUT MODE\n"
			"         1: OUTPUT MODE\n"
			"PC2 (RW) 0: PD2 INPUT MODE\n"
			"         1: OUTPUT MODE\n"
			"PC1 (RW) 0: PD1 INPUT MODE\n"
			"         1: OUTPUT MODE\n"
			"PCO (RW) 0: PDO INPUT MODE\n"
			"         1: OUTPUT MODE");
 CW(0xA1000B,"CTRL2","");
 CW(0xA1000D,"CTRL3","");
 CW(0xA1000F,"TxDATA1","");
 CW(0xA10011,"RxDATA1","");
 CW(0xA10013,"SCTRL1","");
 CW(0xA10015,"TxDATA2","");
 CW(0xA10017,"RxDATA2","");
 CW(0xA10019,"SCTRL2","");
 CW(0xA1001B,"TxDATA3","");
 CW(0xA1001D,"RxDATA3","");
 CW(0xA1001F,"SCTRL3","");

 CW(0xA11000,"MemMode",	"D8 ( W)   0: ROM MODE\n"
                        "          1: D-RAM MODE");
 
 CW(0xA11100,"Z80BusReq","D8 ( W)   0: BUSREQ CANCEL\n"
                         "          1: BUSREQ REQUEST\n"
                         "   ( R)   0: CPU FUNCTION STOP ACCESSIBLE\n"
                         "          1: FUNCTIONING");
 CW(0xA11200,"Z80BusReset","D8 ( W)   0: RESET REQUEST\n"
                           "          1: RESET CANCEL"); 
}
 
//-------------------------------------------------------------------------
static main() {
  auto addr;
  
  SetPrcsr( "68000");

  SegCreate(0x000000,0x3FFFFF,0,1,0,2);
  SegRename(0x000000,"ROM");

  SegCreate(0xA00000,0xA0FFFF,0,1,0,2);
  SegRename(0xA00000,"Z80");

  SegCreate(0xA10000,0xA10FFF,0,1,0,2);
  SegRename(0xA10000,"IO");

  SegCreate(0xA11000,0xA11FFF,0,1,0,2);
  SegRename(0xA11000,"Control");

  SegCreate(0xC00000,0xDFFFFF,0,1,0,2);
  SegRename(0xC00000,"VDP");

  SegCreate(0xFF0000,0xFFFFFF,0,1,0,2);
  SegRename(0xFF0000,"RAM");
  
  mdVector( );
  mdHeader( );
  mdAddress( );
 
  addr =  Dword( 0x04 );
  MakeName(addr,"main");
  //MakeCode(addr);
  MakeFunction(addr,BADADDR);
   Wait( );
   
  addr =  Dword( 0x68 );
  MakeName(addr,"EInt");
  //MakeCode(addr);
  MakeFunction(addr,BADADDR);
  Wait( ); 
  
  addr =  Dword( 0x70 );
  MakeName(addr,"HInt");
  //MakeCode(addr);
  MakeFunction(addr,BADADDR);
  Wait( ); 

  addr =  Dword( 0x78 );
  MakeName(addr,"VInt");
  //MakeCode(addr);
  MakeFunction(addr,BADADDR);
  Wait( );  
}
