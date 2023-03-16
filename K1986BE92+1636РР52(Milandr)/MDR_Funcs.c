#include "MDR_Funcs.h"

///==================  Инициализация задержек, необходимых по протоколу ======================
typedef struct {
  uint32_t        Erase_55ms;   //  SectorErase / ChipErase
  uint32_t        Program_45us; //  ByteProgram
  uint32_t        Reset_30us;   //  Reset
  uint32_t        CS_20ns;      //  CS - Задержка между активацией SPI и передачей данных
  uint32_t        RD_30ns;      //  CS - удержание между чтениями
  uint32_t        WR_1us;       //  CS - удержание между записями
} MDR_RR52_Delays;

///  Инициализация по умолчанию под максимальную частоту в 144МГц (1986ВЕ1)
//с 5.09.22static
MDR_RR52_Delays _RR52_Delay =
{
  .Erase_55ms   = MS_TO_DELAY_LOOPS(55, 144E+6),
  .Program_45us = US_TO_DELAY_LOOPS(44, 144E+6),
  .Reset_30us   = US_TO_DELAY_LOOPS(55, 144E+6),
  .CS_20ns      = NS_TO_DELAY_LOOPS(20, 144E+6),
  .RD_30ns      = NS_TO_DELAY_LOOPS(30, 144E+6),
  .WR_1us       = US_TO_DELAY_LOOPS(1,  144E+6),
};

void MDR_RR52_InitDelays(uint32_t CPU_FregHz)
{
  _RR52_Delay.Erase_55ms   = MS_TO_DELAY_LOOPS(55, CPU_FregHz);
  _RR52_Delay.Program_45us = US_TO_DELAY_LOOPS(44, CPU_FregHz);
  _RR52_Delay.Reset_30us   = US_TO_DELAY_LOOPS(55, CPU_FregHz);
  _RR52_Delay.CS_20ns      = NS_TO_DELAY_LOOPS(20, CPU_FregHz);
  _RR52_Delay.RD_30ns      = NS_TO_DELAY_LOOPS(30, CPU_FregHz);
  _RR52_Delay.WR_1us       = US_TO_DELAY_LOOPS(1,  CPU_FregHz);
}

///===================    Функция ожидания с таймаутом  ===================
bool WaitCondition(uint32_t timeoutCycles, pBoolFunc_void checkFunc)
{
  while (timeoutCycles != 0)
  {
    if (checkFunc())
      return true;
    timeoutCycles--;
  }
  return false;
}

void MDR_WaitFlagSet(uint32_t addr, uint32_t flag)
{
  while ((REG32(addr) & flag) != flag);
}

void MDR_WaitFlagClear(uint32_t addr, uint32_t flag)
{
  while ((REG32(addr) & flag) == flag);
}


///=========================    Задержка =======================
void MDR_Delay(uint32_t Ticks)
{
  volatile uint32_t i = Ticks;
  if (i)
    while (--i);

  //for (; i > 0; i--);  // - Больше циклов, сильнее зависит от оптимизации
}

///=====================    Псевдо-случайное значение ===================
uint32_t MDR_ToPseudoRand(uint32_t value)
{
  uint32_t hash = 0;
  uint32_t i = 0;
  uint8_t* key = (uint8_t *)&value;

  for (i = 0; i < 4; i++)
  {
    hash += key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  for (i = 0; i < 256; i++)
  {
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }

  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

///============    Log for debug ============
#ifdef DEBUG_LOG_ENA
  #define LOG_BUFF_Len  200
  // Объект лога. Можно завести несколько, чтобы логгировать в разные массивы.
  static uint32_t LogData1[LOG_BUFF_Len];

  MDR_LogRec MDR_LogRec1 =
  {
    LogData1,
    LOG_BUFF_Len,
    0,
    0
  };

  void MDR_LOG_Clear(MDR_LogRec *pLogRec)
  {
    uint16_t i;

    for (i = 0; i < pLogRec->BuffLen; ++i)
      pLogRec->pBuff[i] = 0;
    pLogRec->IndWR = 0;
    pLogRec->DataCnt = 0;
  }

  void MDR_LOG_Add(MDR_LogRec *pLogRec, uint32_t value)
  {
    pLogRec->pBuff[pLogRec->IndWR++] = value;
    pLogRec->DataCnt++;
    if (pLogRec->IndWR >= pLogRec->BuffLen)
      pLogRec->IndWR = 0;
  }
#endif
