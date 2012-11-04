#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "usart.h"

// Глобальные переменные
uint8_t catcher_status = 0;     // Состояние ловушки: 0 - нарастающий фронт, 1 - спадающий
uint16_t duration = 0;          // Длительность последнего пойманного импульса

void Delay_ms(uint32_t ms)
{
    volatile uint32_t nCount;
    RCC_ClocksTypeDef RCC_Clocks;
    RCC_GetClocksFreq (&RCC_Clocks);

    nCount = (RCC_Clocks.HCLK_Frequency/10000)*ms;
    for (; nCount!=0; nCount--);
}

void init_ports()
{
	GPIO_InitTypeDef PORT;
	// Порт С, пин 3 - выход. Подключить к линии Trig модуля HC-SR04
	// Порт A, пин 0 - вход. Подключить к линии Echo модуля HC-SR04
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC , ENABLE);
	PORT.GPIO_Pin = GPIO_Pin_3;
	PORT.GPIO_Mode = GPIO_Mode_Out_PP;
	PORT.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_Init(GPIOC, &PORT);
}

void init_interrupts()
{
	//========================================================================
	//                          Настройка таймера 6
	// Используется для 2-х целей:
	// 1) Подсчёт длительности Echo импульса (150 мкс - 25 мс)
	// 2) Подсчёт с прерыванием для отчёта периода цикла - времени,
	// необходимого для затухания остаточных колебаний в линии Echo
	//========================================================================
	// Включаем тактирование таймера
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	// Выставляем предделитель на срабатывание раз в мкс
	TIM6->PSC = 24 - 1;
	// Граница срабатывания - 50 мс = 50 000 мкс
	TIM6->ARR = 50000;
	//Разрешение TIM6_IRQn прерывания - необходимо для отсчёта периода цикла
	NVIC_SetPriority(TIM6_IRQn, 3);
	NVIC_EnableIRQ(TIM6_IRQn);
	//========================================================================
	//                          Настройка таймера 7
	//========================================================================
	// Включаем тактирование таймера
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
	// Выставляем предделитель на срабатывание раз в мкс
	TIM7->PSC = 24 - 1;
	// Граница срабатывания - 10 мкс
	TIM7->ARR = 10;
	//Разрешение TIM7_IRQn прерывания - необходимо для отсчёта сигнального импульса
	NVIC_SetPriority(TIM7_IRQn, 2);
	NVIC_EnableIRQ(TIM7_IRQn);
	//========================================================================
	//                          Настройка внешнего прерывания
	// Внешние прерывания по умолчанию включены для всего порта A.
	// Здесь используется 0-й пин порта А.
	//========================================================================
	// Включаем Alternate function I/O clock
	RCC_APB2PeriphClockCmd(RCC_APB2ENR_AFIOEN , ENABLE);
	// Прерывания от нулевой ноги разрешены
	EXTI->IMR |= EXTI_IMR_MR0;
	// Прерывания по нарастающему фронту
	EXTI->RTSR |= EXTI_RTSR_TR0;
	//Разрешаем прерывание
	NVIC_SetPriority(EXTI0_IRQn, 1);
	NVIC_EnableIRQ (EXTI0_IRQn);
}

int main(void)
{
	USART_Configuration();               // Настройка USART для вывода printf в COM
	init_ports();
	init_interrupts();
	// Запускаем таймер 7 первый раз на отсчёт 10 мс
	TIM7->DIER |= TIM_DIER_UIE;  	     // Разрешаем прерывание от таймера 7
	GPIOC->ODR |= GPIO_Pin_3; 			 // Включаем сигнальный импульс
	TIM7->CR1 |= TIM_CR1_CEN;    	     // Запускаем таймер
    while(1)
    {
    	Delay_ms(500);
    	printf("S = %d mm, %d\n", duration/29, duration);
    }
}

// Обработчик прерывания EXTI0: изменение уровня сигнала
void EXTI0_IRQHandler(void)
{
	// Если поймали нарастающий фронт
	if (!catcher_status)
	{
		// Запускаем отсчёт длительности импульса
		TIM6->CR1 |= TIM_CR1_CEN;
		// Переключаемся на отлов спадающего фронта
		catcher_status = 1;
		EXTI->RTSR &= ~EXTI_RTSR_TR0;
		EXTI->FTSR |= EXTI_FTSR_TR0;
	}
	// Если поймали спадающий фронт
	else
	{
		TIM6->CR1 &= ~TIM_CR1_CEN;       // Останавливаем таймер
		duration = TIM6->CNT;            // Считываем значение длительности в мкс
		TIM6->CNT = 0;                   // Обнуляем регистр-счётчик
		// Переключаемся на отлов нарастающего фронта
		catcher_status = 0;
		EXTI->FTSR &= ~EXTI_FTSR_TR0;
		EXTI->RTSR |= EXTI_RTSR_TR0;
		// Запускаем таймер 6 на отсчёт 50 мс
		TIM6->DIER |= TIM_DIER_UIE;      // Разрешаем прерывание от таймера
		TIM6->CR1 |= TIM_CR1_CEN;        // Запускаем таймер
	}
	EXTI->PR |= 0x01; 					 //Очищаем флаг
}

// Обработчик прерывания TIM7_DAC
// Вызывается после того, как таймер 7 отсчитал 10 мкс для сигнального импульса
void TIM7_IRQHandler(void)
{
	TIM7->SR &= ~TIM_SR_UIF;             // Сбрасываем флаг UIF
	GPIOC->ODR &= ~GPIO_Pin_3;			 // Останавливаем сигнальный импульс
	TIM7->DIER &= ~TIM_DIER_UIE;         // Запрещаем прерывание от таймера 7
}

// Обработчик прерывания TIM6_DAC
// Вызывается после того, как таймер 6 отсчитал 50 мкс для периода цикла
void TIM6_IRQHandler(void)
{
	TIM6->SR &= ~TIM_SR_UIF;             // Сбрасываем флаг UIF
	GPIOC->ODR |= GPIO_Pin_3; 			 // Включаем сигнальный импульс
	// Запускаем таймер 7 на отсчёт 10 мс
	TIM7->DIER |= TIM_DIER_UIE;  	     // Разрешаем прерывание от таймера 7
	TIM7->CR1 |= TIM_CR1_CEN;    	     // Запускаем таймер
}

