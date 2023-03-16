// ***********************************************************************************
// Микроконтроллер: 1986ВЕ92QI
// Файл:  1636RR52.c
// Модуль:  Работа с микросхемой Flash-памяти 1636РР52У1
// Компилятор:  ARM GCC Compiler из IDE Em::Blocks 2.30
// ***********************************************************************************
#include "MDR32F9Qx_ssp.h"
#include "MDR32F9Qx_rst_clk.h"
#include "MDR32F9Qx_port.h"
#include "1636RR52.h"
#include "MDR32F9Qx_timer.h"
#include "user_defsM.h"
#include "MDR_Funcs.h"

// Вариантный тип данных для работы с двухбайтным числом
 typedef volatile union
 {
   uint16_t Word;     // Доступ к 2-х байтному слову
   uint8_t Byte[2];   // Доступ к отдельным байтам
 } TVariant16;

 // Объединение для работы с четырехбайтным числом
 typedef volatile union
 {
   uint32_t DWord;    // Доступ к 4-байтному слову
   float Float;       // Доступ к вещественному значению (4 байта)
   uint8_t Byte[4];   // Доступ к отдельным байтам
   uint16_t Word[2];  // Доступ к отдельным словам
 } TVariant32;
 //-------------------------------
 //буферы для работы с ИМС флэш 1635RR52
 epromBl Buf1636w, Buf1636r; // write/read, функции - в конце файла
 uint32_t rdBlOff; //смещение в байтах до начала блока перед чтением
 uint32_t wrBlOff; //смещение в байтах до начала блока перед записью
 uint32_t nextOff = 0; //смещение в байтах до начала следующего блока при поиске

 epromThd tmpHdr; //заголовок для модификации блоков, e.g. стирания блока
 // прототипы
// Ожидание окончания записи байта
static void Delay_Byte_Program (void);
// Ожидание стирания всей памяти
static void Delay_Chip_Erase (void);
// Ожидание стирания сектора
static void Delay_Sector_Erase (void);

///--------- Инициализация SPI --------------
//uint32_t
void U_1636RR52_Init (void)
{
	// Структура для инициализации линий ввода-вывода
  PORT_InitTypeDef PortInitStructure;
	// Структура для инициализации SPI
  SSP_InitTypeDef SSPInitStructure;
  // Разрешить тактирование SSP и PORT
  RST_CLK_PCLKcmd ( RST_CLK_PCLK_SSP2 | RST_CLK_PCLK_PORTD |
                   RST_CLK_PCLK_PORTC, ENABLE);  //добавлен порт С

  // Конфигурация выводов SSP2
	PORT_StructInit (&PortInitStructure);
  PortInitStructure.PORT_PULL_UP = PORT_PULL_UP_OFF;
  PortInitStructure.PORT_PULL_DOWN = PORT_PULL_DOWN_OFF;
  PortInitStructure.PORT_PD_SHM = PORT_PD_SHM_OFF;
  PortInitStructure.PORT_PD = PORT_PD_DRIVER;
  PortInitStructure.PORT_GFEN = PORT_GFEN_OFF;
  PortInitStructure.PORT_SPEED = PORT_SPEED_MAXFAST;
  PortInitStructure.PORT_MODE = PORT_MODE_DIGITAL;

  // Деинициализация SSP2
  SSP_DeInit (U_1636RR52_SPI);

  SSP_StructInit (&SSPInitStructure);

	// Задать коэффициент деления частоты PCLK для получения частоты F_SSPCLK
	SSP_BRGInit(U_1636RR52_SPI, U_1636RR52_SSP_BRG);

	// SSP_TX - выход MOSI
	PortInitStructure.PORT_OE = PORT_OE_OUT;
    PortInitStructure.PORT_FUNC = U_1636RR52_MOSI_FUNC;
	PortInitStructure.PORT_Pin = U_1636RR52_MOSI_PIN;
	PORT_Init(U_1636RR52_MOSI_PORT, &PortInitStructure);

  // SSP_RX - вход MISO
	PortInitStructure.PORT_OE = PORT_OE_IN;
  PortInitStructure.PORT_FUNC = U_1636RR52_MISO_FUNC;
	PortInitStructure.PORT_Pin = U_1636RR52_MISO_PIN;
	PORT_Init(U_1636RR52_MISO_PORT, &PortInitStructure);

	// SSP_SCLK - выход SCLK
	PortInitStructure.PORT_OE = PORT_OE_OUT;
  PortInitStructure.PORT_FUNC = U_1636RR52_SCLK_FUNC;
	PortInitStructure.PORT_Pin = U_1636RR52_SCLK_PIN;
	PORT_Init(U_1636RR52_SCLK_PORT, &PortInitStructure);

	//с 5.09.22
  PortInitStructure.PORT_PULL_UP = PORT_PULL_UP_ON; //надо ли?
  PortInitStructure.PORT_OE    = PORT_OE_OUT;
  PortInitStructure.PORT_FUNC   = PORT_FUNC_PORT;
  PortInitStructure.PORT_Pin = U_1636RR52_SS_PIN; //это PORT_Pin_0

  PortInitStructure.PORT_MODE  = PORT_MODE_DIGITAL;
  PortInitStructure.PORT_SPEED = PORT_SPEED_FAST;  //PORT_SPEED_SLOW ! 100ns  PORT_SPEED_MAXFAST ~10ns
  PORT_Init(U_1636RR52_SS_PORT, &PortInitStructure);
  PORT_SetBits(U_1636RR52_SS_PORT, U_1636RR52_SS_PIN); /// Пока ИМС не выбрана

	// Конфигурация SSP (Master)
	SSPInitStructure.SSP_Mode = SSP_ModeMaster;          // Режим ведущего (Master)

	// Предделители частоты сигнала SCLK
	// F_SCLK = F_SSPCLK / ( CPSDVR * (1 + SCR) = 10МГц / (10 * (1 + 1)) = 500 кГц
	SSPInitStructure.SSP_CPSDVSR = U_1636RR52_SSP_CPSDVSR;  // 2    2 to 254
   SSPInitStructure.SSP_SCR = 2; ///???	U_1636RR52_SSP_SCR;          // 9    0 to 255

	SSPInitStructure.SSP_WordLength = SSP_WordLength8b;  // Размер фрейма

	// Режим 3
	// При этом FSS устанавливается в низкое состояние на время передачи всего блока данных
	SSPInitStructure.SSP_SPO = SSP_SPO_High;       // "Полярность" SCLK
	SSPInitStructure.SSP_SPH = SSP_SPH_2Edge;      // Фаза SCLK (по заднему фронту)

	SSPInitStructure.SSP_HardwareFlowControl = SSP_HardwareFlowControl_SSE;  // Аппаратное управление передачей данных (приемо-передатчик пока отключен отключен)
	SSPInitStructure.SSP_FRF = SSP_FRF_SPI_Motorola; 	   // Формат фрейма (Motorola)
	SSP_Init (U_1636RR52_SPI, &SSPInitStructure);

  // Разрешить работу SSP
  SSP_Cmd (U_1636RR52_SPI, ENABLE);
}
///заимствования из MDR_1636RR52.c  ???????

extern MDR_RR52_Delays _RR52_Delay;  //????????????????

///--------- Конец инициализации SPI --------

/// Передать блок данных
void static SPI_Write_Block(uint8_t* src, uint8_t* dst, uint32_t Count)
{
  uint32_t i;
	volatile uint32_t data;
	// Дождаться полного освобождения буфера
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));
  // Вычитать все данные из входного буфера, так как там может лежать мусор
	while ((U_1636RR52_SPI->SR & SSP_FLAG_RNE))
		data = U_1636RR52_SPI->DR;

	for (i = 0; i < Count; i++)
	{ // Дождаться появления свободного места в буфера передатчика
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_TNF));
	  // Передать байт
		U_1636RR52_SPI->DR = *src++;
	}
	for (i = 0; i < Count; i++)
	{ // Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
	  // Читаем байт, полученный от 1636РР52У
	  *dst++ = U_1636RR52_SPI->DR;
	}
}

// **** Команды *****
/// Чтение 4 байт данных, начиная с заданного адреса на частоте до 15 МГц
uint32_t U_1636RR52_Read_Word (uint32_t addr)
{
  TVariant32 result;
  TVariant32 Addr;

	uint16_t src[4];
  uint32_t i;
	volatile uint32_t data;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_READ_ARRAY_15;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];

	// Дождаться полного освобождения SPI
	while (((U_1636RR52_SPI->SR) & SSP_FLAG_BSY));

  // Вычитать все данные из входного буфера, так как там может лежать мусор
	while ((U_1636RR52_SPI->SR & SSP_FLAG_RNE))
		data = U_1636RR52_SPI->DR;


  // Передать 4 байта с кодом команды и адресом
	for (i = 0; i < 4; i++)
		U_1636RR52_SPI->DR = src[i];

  // Передать 4 пустых байта
	for (i = 0; i < 4; i++)
		U_1636RR52_SPI->DR = 0;

	// Читаем ответные 4 байта, хоть они нам и не нужны
	for (i = 0; i < 4; i++)
	{
		// Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
		U_1636RR52_SPI->DR;
	}

	// Читаем и сохраняем 4 полезных ответных байта
	for (i = 0; i < 4; i++)
	{
		// Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
		result.Byte[i] = U_1636RR52_SPI->DR;
	}

  return result.DWord;
}

/// Чтение массива данных на частоте до 15 МГц
uint32_t U_1636RR52_Read_Array_15 (uint32_t addr, uint8_t* dst, uint32_t Count)
{
	uint16_t src[4];
  TVariant32 Addr;
  uint32_t i,j;
	volatile uint32_t data;
	uint8_t* Dst;

	Dst = dst;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_READ_ARRAY_15;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];

	// Дождаться полного освобождения SPI
	while (((U_1636RR52_SPI->SR) & SSP_FLAG_BSY));

  // Вычитать все данные из входного буфера, так как там может лежать мусор
	while ((U_1636RR52_SPI->SR & SSP_FLAG_RNE))
		data = U_1636RR52_SPI->DR;

  // Передать 4 байта с кодом команды и адресом
	for (i = 0; i < 4; i++)
		U_1636RR52_SPI->DR = src[i];

	// Читаем ответные 4 байта, хоть они нам и не нужны
	for (i = 0; i < 4; i++)
	{
		// Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
		data = U_1636RR52_SPI->DR;
	}

	// Передаем пустой байт и принимаем ответный
	for (i = 0, j = 0; i < Count; i++)
	{
		// Дождаться появления свободного места в выходном буфере
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_TNF))
		{
	    // Читаем байт, полученный от 1636РР52У
		  if (U_1636RR52_SPI->SR & SSP_FLAG_RNE)
			{
	          *(Dst++) = U_1636RR52_SPI->DR;   j++;
			}
		}
		// Передаем пустой байт
		U_1636RR52_SPI->DR = 0;

		// Читаем байт, полученный от 1636РР52У
		if (U_1636RR52_SPI->SR & SSP_FLAG_RNE)
		{
		 *(Dst++) = U_1636RR52_SPI->DR;  j++;
		}
	}
  // Читаем остаток еще не принятых байт
	while (U_1636RR52_SPI->SR & (SSP_FLAG_RNE | SSP_FLAG_BSY))
	{
		if ((U_1636RR52_SPI->SR & SSP_FLAG_RNE) && j < Count)
		{
		 *(Dst++) = U_1636RR52_SPI->DR;   j++;
		}
	}

	return j; // Сколько байт прочитали
}


/// Чтение массива данных на частоте до 100 МГц
uint32_t U_1636RR52_Read_Array_100 (uint32_t addr, uint8_t* dst, uint32_t Count)
{
	uint16_t src[5];
  TVariant32 Addr;
  uint32_t i,j;
	volatile uint32_t data;
	uint8_t* Dst;

	Dst = dst;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_READ_ARRAY_100;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];
	src[4] = 0;  // Фиктивный байт

	// Дождаться полного освобождения SPI
	while (((U_1636RR52_SPI->SR) & SSP_FLAG_BSY));

  // Вычитать все данные из входного буфера, так как там может лежать мусор
	while ((U_1636RR52_SPI->SR & SSP_FLAG_RNE))
		data = U_1636RR52_SPI->DR;

  // Передать 4 байта с кодом команды и адресом
	for (i = 0; i < 5; i++)
		U_1636RR52_SPI->DR = src[i];

	// Читаем ответные 4 байта, хоть они нам и не нужны
	for (i = 0; i < 5; i++)
	{
		// Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
		data = U_1636RR52_SPI->DR;
	}

	// Передаем пустой байт и принимаем ответный
	for (i = 0, j = 0; i < Count; i++)
	{
		// Дождаться появления свободного места в выходном буфере
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_TNF))
		{
	    // Читаем байт, полученный от 1636РР52У
		  if (U_1636RR52_SPI->SR & SSP_FLAG_RNE)
			{
	     *(Dst++) = U_1636RR52_SPI->DR;
			 j++;
			}
		}
		// Передаем пустой байт
		U_1636RR52_SPI->DR = 0;

		// Читаем байт, полученный от 1636РР52У
		if (U_1636RR52_SPI->SR & SSP_FLAG_RNE)
		{
		 *(Dst++) = U_1636RR52_SPI->DR;
		 j++;
		}
	}

  // Читаем остаток еще не принятых байт
	while (U_1636RR52_SPI->SR & (SSP_FLAG_RNE | SSP_FLAG_BSY))
	{
		if ((U_1636RR52_SPI->SR & SSP_FLAG_RNE) && j < Count)
		{
		 *(Dst++) = U_1636RR52_SPI->DR;
		 j++;
		}
	}
	return j; // Сколько байт прочитали
}

// Стирание сектора
uint32_t U_1636RR52_Sector_Erase (uint32_t Sector)
{
	uint32_t result;

	uint8_t src[U_1636RR52_CMD_L_SECTOR_ERASE], dst[U_1636RR52_CMD_L_SECTOR_ERASE];

  TVariant32 Addr;

	Addr.DWord = Sector;

	src[0] = U_1636RR52_CMD_SECTOR_ERASE;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];

  MDR_RR52_CS_SetActive(); //с 5.09.22
  U_1636RR52_Write_Enable ();  // Разрешить запись
    MDR_RR52_CS_SetInactive(); //с 7.09.22
    MDR_RR52_CS_SetActive(); //с 7.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_SECTOR_ERASE); // Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22

	// Время стирания сектора, мс:  55
  // Ожидание стирания сектора
  Delay_Sector_Erase ();

  return result;
}

// Стирание всей памяти
uint32_t U_1636RR52_Chip_Erase (void)
{
	uint32_t result;

	// Разрешить запись
    MDR_RR52_CS_SetActive();
	U_1636RR52_Write_Enable ();
	MDR_RR52_CS_SetInactive();
    MDR_RR52_CS_SetActive();
 //----------------------------------
    	// Дождаться полного освобождения буфера
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));
  // Вычитать все данные из входного буфера, так как там может лежать мусор
	while ((U_1636RR52_SPI->SR & SSP_FLAG_RNE))
		result = U_1636RR52_SPI->DR;
	 // Дождаться появления свободного места в буфера передатчика
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_TNF));
 //----------------------------------
	// Передать байт c командой
	U_1636RR52_SPI->DR = U_1636RR52_CMD_CHIP_ERASE;
 //----------------------------------
   // Дождаться приема байта
		while (!(U_1636RR52_SPI->SR & SSP_FLAG_RNE));
	  // Читаем байт, полученный от 1636РР52У
	  result = U_1636RR52_SPI->DR;
//-----------------------------------
	MDR_RR52_CS_SetInactive();
	// Время операции стирания микросхемы, мс:  110
	// Ожидание стирания всей памяти
	Delay_Chip_Erase ();
  return result;
}

// Программирование байта
void U_1636RR52_Byte_Program (uint32_t addr, uint8_t Value)
{
	uint8_t src[U_1636RR52_CMD_L_BYTE_PROGRAM], dst[U_1636RR52_CMD_L_BYTE_PROGRAM];

  TVariant32 Addr;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_BYTE_PROGRAM;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];
	src[4] = Value;

  MDR_RR52_CS_SetActive(); //с 5.09.22
  U_1636RR52_Write_Enable ();// Разрешить запись
    MDR_RR52_CS_SetInactive(); //с 7.09.22
    MDR_RR52_CS_SetActive(); //с 7.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_BYTE_PROGRAM);   // Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
	// Время операции программирования байта, мкс:  45
	// Ожидание окончания записи байта
	Delay_Byte_Program();
}

// Программирование блока данных
void U_1636RR52_Block_Program (uint32_t addr, uint8_t* Src, uint32_t Count)
{
	uint8_t src[U_1636RR52_CMD_L_BYTE_PROGRAM], dst[U_1636RR52_CMD_L_BYTE_PROGRAM];
  TVariant32 Addr;
	uint32_t i;

  Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_BYTE_PROGRAM;
  // Записать все байты блока
	for (i = 0; i < Count; i++, Addr.DWord++)
	{
		src[1] = Addr.Byte[2];
		src[2] = Addr.Byte[1];
		src[3] = Addr.Byte[0];
		src[4] = Src[i];

		MDR_RR52_CS_SetActive(); //с 5.09.22
		U_1636RR52_Write_Enable ();// Разрешить запись
	    MDR_RR52_CS_SetInactive(); //с 7.09.22
        MDR_RR52_CS_SetActive(); //с 7.09.22
		SPI_Write_Block (src, dst, U_1636RR52_CMD_L_BYTE_PROGRAM);// Передать блок данных
        MDR_RR52_CS_SetInactive(); //с 5.09.22
    // Время операции программирования байта, мкс:  45, реально сделал 60
		Delay_Byte_Program (); // Ожидание окончания записи байта
	}
}


// Разрешение записи
void U_1636RR52_Write_Enable (void)
{
	// Дождаться полного окончания предыдущих операций
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));

	// Передать байт
	U_1636RR52_SPI->DR = U_1636RR52_CMD_WRITE_ENABLE;

	// Дождаться полного окончания операции
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));

	U_1636RR52_SPI->DR; // Хоть результат чтения и не нужен, но прочитать всё равно надо
}

// Запрет записи
void U_1636RR52_Write_Disable (void)
{
	// Дождаться полного окончания предыдущих операций
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));

	// Передать байт
	U_1636RR52_SPI->DR = U_1636RR52_CMD_WRITE_DISABLE;

	// Дождаться полного окончания операции
	while ((U_1636RR52_SPI->SR & SSP_FLAG_BSY));

	U_1636RR52_SPI->DR; // Хоть результат чтения и не нужен, но прочитать всё равно надо

}

// Защита сектора
uint32_t U_1636RR52_Protect_Sector (uint32_t addr)
{
  TVariant32 result;

	uint8_t src[U_1636RR52_CMD_L_PROTECT_SECTOR], dst[U_1636RR52_CMD_L_PROTECT_SECTOR];

  TVariant32 Addr;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_PROTECT_SECTOR;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];

  MDR_RR52_CS_SetActive(); //с 5.09.22
  U_1636RR52_Write_Enable();// Разрешить запись
    MDR_RR52_CS_SetInactive(); //с 7.09.22
    MDR_RR52_CS_SetActive(); //с 7.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_PROTECT_SECTOR);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
  return result.DWord;
}

// Снятие защиты сектора
//mine uint32_t
void U_1636RR52_Unprotect_Sector (uint32_t addr)
{
  //mine TVariant32 result;

	uint8_t src[U_1636RR52_CMD_L_UNPROTECT_SECTOR], dst[U_1636RR52_CMD_L_UNPROTECT_SECTOR];

  TVariant32 Addr;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_UNPROTECT_SECTOR;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];

  MDR_RR52_CS_SetActive(); //с 5.09.22
  U_1636RR52_Write_Enable ();// Разрешить запись
    MDR_RR52_CS_SetInactive(); //с 7.09.22
    MDR_RR52_CS_SetActive(); //с 7.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_UNPROTECT_SECTOR);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
 //mine  return result.DWord;
}

// Чтение регистра защиты сектора
uint32_t U_1636RR52_Read_Sector_Protection_Register (uint32_t addr)
{
  TVariant32 result;

	uint8_t src[U_1636RR52_CMD_L_READ_SECTOR_PROTECTION_REGISTER], dst[U_1636RR52_CMD_L_READ_SECTOR_PROTECTION_REGISTER];

  TVariant32 Addr;

	Addr.DWord = addr;

	src[0] = U_1636RR52_CMD_READ_SECTOR_PROTECTION_REGISTER;
	src[1] = Addr.Byte[2];
	src[2] = Addr.Byte[1];
	src[3] = Addr.Byte[0];
	src[4] = 0;

  MDR_RR52_CS_SetActive(); //с 5.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_READ_SECTOR_PROTECTION_REGISTER);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
	result.DWord = 0;
  result.Byte[0] = dst[4];

  return result.DWord;
}

// Чтение регистра статуса
uint32_t U_1636RR52_Read_Status_Register (void)
{
  TVariant32 result;

	uint8_t src[U_1636RR52_CMD_L_READ_STATUS_REGISTER], dst[U_1636RR52_CMD_L_READ_STATUS_REGISTER];

	src[0] = U_1636RR52_CMD_READ_STATUS_REGISTER;

  MDR_RR52_CS_SetActive(); //с 5.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_READ_STATUS_REGISTER);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
	result.DWord = 0;
  result.Byte[0] = dst[1];

  return result.DWord;
}

// Запись регистра статуса
// status - комбинация битов U_1636RR52_SR_RSTE и U_1636RR52_SR_SPRL
void U_1636RR52_Write_Status_Register (uint8_t status)
{
	uint8_t src[U_1636RR52_CMD_L_WRITE_STATUS_REGISTER], dst[U_1636RR52_CMD_L_WRITE_STATUS_REGISTER];

	src[0] = U_1636RR52_CMD_WRITE_STATUS_REGISTER;
	src[1] = status & 0xC0; // Можно устанавливать только два старших бита

	MDR_RR52_CS_SetActive(); //с 5.09.22
  U_1636RR52_Write_Enable ();// Разрешить запись
    MDR_RR52_CS_SetInactive(); //с 7.09.22
    MDR_RR52_CS_SetActive(); //с 7.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_WRITE_STATUS_REGISTER);// Передать блок данных
    MDR_RR52_CS_SetInactive(); //с 5.09.22
}

// Сброс
void U_1636RR52_Reset (void)
{
	uint8_t src[U_1636RR52_CMD_L_RESET], dst[U_1636RR52_CMD_L_RESET];

	src[0] = U_1636RR52_CMD_RESET;
	src[1] = 0xD0; // Подтверждающий байт

  MDR_RR52_CS_SetActive(); //с 5.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_RESET);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22
}

// Чтение ID кодов производителя и микросхемы
uint32_t U_1636RR52_Read_ID (void)
{
  TVariant32 result;

	uint8_t src[U_1636RR52_CMD_L_READ_ID], dst[U_1636RR52_CMD_L_READ_ID];

	src[0] = U_1636RR52_CMD_READ_ID;
	src[1] = 0x00;
	src[2] = 0x00;

  //7.09.22
  MDR_RR52_CS_SetActive(); //с 5.09.22
  SPI_Write_Block(src, dst, U_1636RR52_CMD_L_READ_ID);// Передать блок данных
  MDR_RR52_CS_SetInactive(); //с 5.09.22

	result.DWord = 0;
  result.Byte[0] = dst[1];	// Код производителя 0x01
  result.Byte[1] = dst[2];	// Код микросхемы 0xC8

  return result.DWord;
}


// Ожидание окончания записи байта
static void Delay_Byte_Program (void)
{ 	// Время операции программирования байта, мкс:  45
		strtDlay(60); wtDlayFin();   //было 46
//volatile uint32_t t; for (t = 300; t ; --t);	// ~57 мкс !! Можно переделать по-своему
}

// Ожидание стирания всей памяти
static void Delay_Chip_Erase (void)
{  // Время операции стирания микросхемы, мс:  110
	strtDlay(55000); wtDlayFin();
	strtDlay(55000); wtDlayFin();
  //os_dly_wait (110);// !!! Можно переделать по-своему
}

// Ожидание стирания сектора
static void Delay_Sector_Erase (void)
{ 	// Время стирания сектора, мс:  55
	strtDlay(55000); wtDlayFin();
//  os_dly_wait (55);   // !!! Можно переделать по-своему // ...
}
 //выше есть буферы для работы с ИМС флэш
 //epromBl Buf1636w, Buf1636r; // write/read, функции - в конце файла
 //uint32_t rdBlOff; //смещение в байтах до начала блока перед чтением
 //uint32_t wrBlOff; //смещение в байтах до начала блока перед записью

 ///------- функции для работы с блоками из флэш -------
 //------------ прототипы внешних ------------
 uint16_t CheckCRC8(uint8_t* srcarr, int16_t len);
 // Create CRC
 void MakeCRC(uint8_t* arr, int16_t len);
//Указывается длина формируемого пакета с запасом на CRC

 ///поиск очередного неудаленного блока для чтения
  int16_t seek4Blk2R(uint32_t *Offset, uint32_t *NxtOff)
 {  // предварительно надо, например, задать rdBlOff, лучше
    //rdBlOff = 0; передать по ссылке на вход функции /*Offset/
     uint32_t cnt;  uint8_t* dst;

        while (1)
     {  // Чтение массива данных на частоте до 15 МГц
       MDR_RR52_CS_SetActive();  dst = Buf1636r.Dat-4;
	 cnt = U_1636RR52_Read_Array_15(*Offset, dst,//28.09.22
                                 sizeof(epromBlHd));
       MDR_RR52_CS_SetInactive();


            if ((Buf1636r.Hdr.siz0 != 0xff) &&
         (Buf1636r.Hdr.siz0==Buf1636r.Hdr.siz1) &&
         (Buf1636r.Hdr.type !=0xff) ) //в принципе найден блок

         { *NxtOff = *Offset + (Buf1636r.Hdr.siz0 + sizeof(epromBlHd));
           if ((*NxtOff > (0xffff - 128)) && (*NxtOff < 0x10000)) /*вблизи границы между секторами*/
             *NxtOff = 0x10000; //переход сразу в след.сектор
             if (*NxtOff > (0x1ffff - 7)) *NxtOff = *Offset; //уперлись в потолок!

             if (Buf1636r.Hdr.type!=0)  /*"живой", не удаленный блок */ return 1;
             //признак "найден неудаленный блок"
    //---дальше переделка, к границе между секторами нельзя походить ближе 128 байт!---
             else *Offset = *NxtOff;

             if (*Offset > (0x1ffff - 7))
             return 0; //практически до конца 1636 нет нестертых блоков
         }
         else //Offset не соответствует началу заголовка!
         { if ((Buf1636r.Hdr.siz0==0xff)&&   
               (Buf1636r.Hdr.siz1==0xff)&&
               (Buf1636r.Hdr.type==0xff)&&
               (Buf1636r.Hdr.attrib==0xff)) return(-1); //начало свободной области
             return(-2); //ошибочная точка для начала поиска!
         }
     }
 }
  //----------------------------
 ///считать тело (данные) блока в Buf1636r по нач. смещению
 // C=0 no CRC check!
int16_t rdBlk(uint32_t BlOff, uint16_t C) //При ошибке CRC return -1
{     uint32_t cnt;  uint8_t* dst = Buf1636r.Dat;
     MDR_RR52_CS_SetActive();
	 cnt = U_1636RR52_Read_Array_15(BlOff, dst-4,
                                 sizeof(epromBlHd));//only Hdr!
      MDR_RR52_CS_SetInactive();
      MDR_RR52_CS_SetActive();
     cnt = U_1636RR52_Read_Array_15(BlOff + sizeof(epromBlHd),
                                 dst,
                                 Buf1636r.Hdr.siz0);
       MDR_RR52_CS_SetInactive(); //теперь проверить CRC!
       //защита CRC распространяется на 2 байта заголовка и данные!
       //длина проверяемого пакета с учетом CRC, return 1 - OK
       if (C) //need CRC check
   if (!CheckCRC8(Buf1636r.Dat-2, //28.09.22 +2
                  Buf1636r.Hdr.siz0+2)) return(-1); //Error
   return(0); //OK!
}
//--------------------------
///поиск начальной точки для записи нового блока
//модифицирует uint32_t wrBlOff -смещение в байтах до начала блока
//перед записью. Начальное wrBlOff лучше сделать 0
//возврат: 1 выход на свободную область, 0 или (-1) - места не осталось
 int16_t seekFree(void)
{  while (1)
    switch(seek4Blk2R(&wrBlOff, &nextOff))
   { case -2: return(-2); //ошибочная точка для начала поиска блока!
       //Offset не соответствует началу заголовка!
       break;
       case -1: //начало свободной
           //------------------------
           //размер может оказаться недостаточен для нового блока!
         if ((wrBlOff > (0xffff - 128)) && (wrBlOff < 0x10000))
             /*вблизи границы между секторами*/
           wrBlOff = 0x10000; //переход сразу в след.сектор
         if (wrBlOff > (0x1ffff - 128)) /*вблизи верхней границы*/
           return(-1);//уже не стоит и начинать!
           //------------------------
           return(1); //нашли хорошее место для нового блока
       break;
   case 0: return(0); //практически до конца 1636 идут только стертые блоки
   case 1:  //признак наличия найденного блока
       if (wrBlOff == nextOff) return(-1);//объем практически исчерпан!
       wrBlOff = nextOff; 
       break;
   }
}
//--------------------------
///Запись нового блока в EEPROM со смещения wrBlOff из буфера Buf1636w
//Buf1636w должен быть заполнен, кроме CRC!
//модифицирует uint32_t wrBlOff - смещение в байтах до начала блока
//после записи. До этого считает CRC. Делает контрольное считывание!
//возврат: 1 OK, -1  нет совпадения при контрольном считывании.
 int16_t wrBlk2Free(void)
{ uint32_t Sec = wrBlOff & 0xFF0000;
    MakeCRC(Buf1636w.Dat-2, Buf1636w.Hdr.siz0+2); //28.09.22 +2   CRC+ type+attrib
         U_1636RR52_Unprotect_Sector(Sec);
					 // Программирование блока данных
    U_1636RR52_Block_Program(wrBlOff, (uint8_t*)Buf1636w.Dat-4,//28.09.22
                             Buf1636w.Hdr.siz0+sizeof(epromBlHd));

       //проверить чтением без пересчета CRC
     if (rdBlk(wrBlOff, 0)) return(-2); //для углубленной диагностики
      for (int i=0; i<Buf1636w.Hdr.siz0+2; i++)
          if (Buf1636w.Dat[i-2]!=Buf1636r.Dat[i-2]) return(-1);//No coincidence!
          //--------------------------------
      wrBlOff +=(Buf1636w.Hdr.siz0 + sizeof(epromBlHd));
        if ((wrBlOff > (0xffff - 128)) && (wrBlOff < 0x10000))
             /*вблизи границы между секторами*/
           wrBlOff = 0x10000; //переход сразу в след.сектор
         if (wrBlOff > (0x1ffff - 128)) /*вблизи верхней границы*/
           return(1);//уже не стоит и начинать!
      //--------------------------------
         return(0); //OK
}
///Функции для записи временных (наращиваемых) блоков пока нет!
//--------------------------
///Запись/модификация заголовка блока EEPROM со смещения BlOff из буфера Hdr
//e.g. для удаления блока или завершения записи "временного" (пополняемого) блока
//NS - позволяет не перезаписывать размер
 void mdfBlkHdr(uint32_t BlOff, epromThd *Hdr, uint16_t NS)
{  uint32_t Loff=0, Sec, Lcnt=4;

   if (NS) {Loff = 2; Lcnt = 2;}//do not change size field
          Sec = (BlOff + Loff) & 0xFF0000; //можно и без Loff!
          U_1636RR52_Unprotect_Sector(Sec);
             // Программирование заголовка блока
    U_1636RR52_Block_Program(BlOff+Loff, (uint8_t*)Hdr + Loff,Lcnt); //(uint8_t*)
 //   extern int16_t trase;   extern int16_t trase1;
 //   trase = *((uint8_t*)Hdr+Loff+1); trase1 = Hdr->Dat[3];  //Read to check???
}
///Замена блока заданного типа на аналогичный с новым содержимым ("перезапись")
//старый стирается (type = 0), новый пишется за последним занятым блоком
//задействованы Buf1636w, Buf1636r; // write/read, и tmpHdr;
//(заголовок для стирания старого блока). Если rdBlOff!=0, то поиск стартует с этой точки!
//wrBlOff используется и модифицируется!.
// return(1); - нормальный выход, 0,-1,-2 - error!
  int16_t rplceBlk(uint8_t type, uint8_t attrib)
{ uint32_t fndBlOff;
 //******** найти старый, записать новый, стереть старый ********
       for (int done =0; done<4;)
          switch(seek4Blk2R(&rdBlOff, &nextOff))
    {case 1: //"найден блок" rdBlOff наведен на заголовок
         if (Buf1636r.Hdr.type==type) //нашелся блок с искомым типом
         if (Buf1636r.Hdr.attrib==attrib) //и искомым атрибутом
      {  fndBlOff = rdBlOff; /*смещение до загол.найденного блока*/ done |=1;
      }  /* else  найден блок, но не тот! */
        if (rdBlOff == nextOff) /*final block!*/ return(-1); //error уже некуда писать!;
      /*fndBlOff*/ rdBlOff = nextOff;
           break;//__________________________

     case -1: //rdBlOff указывал на начало пустой области или просто пустая флэш!
              //rdBlOff должно быть выбрано так, чтобы не пропустить старый блок!
              //Поэтому:
              wrBlOff = rdBlOff; //запись блока пойдет в это место
              Buf1636w.Hdr.type = type; Buf1636w.Hdr.attrib = attrib;
            if (!wrBlk2Free()) //запись блока OK. Меняет wrBlOff!
         { tmpHdr.Hdr.type = 0; tmpHdr.Hdr.attrib = attrib;

         if (done & 1)/*попалась не пустая флэш и был найден старый блок.. */
            mdfBlkHdr(fndBlOff, &tmpHdr, 1); //не меняет размер, блок стерт!
             return(1); //нормальный выход
         }
         else return(-1); //error
         //__________________________
     case -2: //ошибочная точка для начала поиска
     case 0: //практически до конца 1636 нет нестертых блоков, т.е. все забито удаленными
         return(-2); //error
       break;//__________________________
    }
//****************************
}
 //------- end функции для работы с блоками из флэш -------

