/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

//---------------------------------------------------------------------------
// Create CRC
void MakeCRC(uint8_t* arr, int16_t len)
//Указывается длина формируемого пакета с запасом на CRC
{    uint16_t r = 0xffff, p = 0xa001; //register & polinome,
   int16_t i,w, oper =0; //result
  for (i=0; i < len-2;i++)
  {      r = arr[i]  ^ r;
    for (w=0;w<8;++w)
    {  if (r & 1) oper = 1; else oper = 0;
	   r >>=1;  if (oper) r = r ^p;
    }
  }
 arr[i++] = (r & 0xff00) >>8; arr[i++] =r & 0xff;  // higher, lower
}
//--------------------------------------------------------------------------
// Check CRC
//длина проверяемого пакета с учетом CRC, return 1 - OK
uint16_t CheckCRC8(uint8_t* srcarr, int16_t len)
{ uint16_t r = 0xffff, p = 0xa001; //register & polinome,
   int16_t i,w, oper =0; //result

  for (i=0; i <len-2;i++)
  {       r = srcarr[i] ^ r;
    for (w=0;w<8;++w)
    { if (r & 1) oper = 1; else oper = 0;
	   r >>=1; if (oper) r = r ^ p;
    }
  }
  // arr[i++] = (r & 0xff00) >>8; arr[i++] =r & 0xff;  // higher, lower
  if((srcarr[len-1] == (r & 0xff)) && (srcarr[len-2] == ((r & 0xff00) >>8)) ) return(1);
  return(0);  ///Ошибка
}
//---------------------------------------------------------------------------
