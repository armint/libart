#pragma once

#define GLUE(a, b) a##b
#define GLUE2(x, y) GLUE(x, y)

#define GPIO(x) GLUE(GPIO, x)
#define GPIO_PIN(x) (1ul << x)
#define IO(port, pin) GLUE2(P, GLUE(port, pin))
#define GPIO_AF(x) GLUE(GpioPin::AlternateFunction::AF, x)

#define UART(x) GLUE(UART, x)
#define USART(x) GLUE(USART, x)
#define USART_BASE(x) GLUE2(USART, GLUE(x, _BASE))
#define LPUART(x) GLUE(LPUART, x)

#define USART_IRQn(x) GLUE2(USART, GLUE(x, _IRQn))
#define USART_IRQHandler(x) GLUE2(USART, GLUE(x, _IRQHandler))


#define SPI(x) GLUE(SPI, x)
#define SPI_BASE(x) GLUE2(SPI, GLUE(x, _BASE))

#define GPIO_BASE(x) GLUE2(GPIO, GLUE(x, _BASE))
#define I2C(x) GLUE(I2C, x)
#define I2C_BASE(x) GLUE2(I2C, GLUE(x, _BASE))
#define COMP(x) GLUE(COMP, x)

#define TIMER(x) GLUE(TIM, x)
#define TIMER_BASE(x) GLUE2(TIM, GLUE(x, _BASE))
#define LL_TIM_CHANNEL(x) (1ul << (4 * (x - 1)))
#define TIMER_IRQn(x) GLUE2(TIM, GLUE(x, _IRQn))
#define TIMER_IRQHandler(x) GLUE2(TIM, GLUE(x, _IRQHandler))


#define ADC_CHANNEL(port, pin) GLUE2(ADC_CHANNEL_P, GLUE(port, pin))

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)


#define ADC_CHANNEL_PA0 LL_ADC_CHANNEL_0
#define ADC_CHANNEL_PA1 LL_ADC_CHANNEL_1
#define ADC_CHANNEL_PA2 LL_ADC_CHANNEL_2
#define ADC_CHANNEL_PA3 LL_ADC_CHANNEL_3
#define ADC_CHANNEL_PA4 LL_ADC_CHANNEL_4
#define ADC_CHANNEL_PA5 LL_ADC_CHANNEL_5
#define ADC_CHANNEL_PA6 LL_ADC_CHANNEL_6
#define ADC_CHANNEL_PA7 LL_ADC_CHANNEL_7
#define ADC_CHANNEL_PB0 LL_ADC_CHANNEL_8
#define ADC_CHANNEL_PB1 LL_ADC_CHANNEL_9
#define ADC_CHANNEL_PC0 LL_ADC_CHANNEL_10
#define ADC_CHANNEL_PC1 LL_ADC_CHANNEL_11
#define ADC_CHANNEL_PC2 LL_ADC_CHANNEL_12
#define ADC_CHANNEL_PC3 LL_ADC_CHANNEL_13
#define ADC_CHANNEL_PC4 LL_ADC_CHANNEL_14
#define ADC_CHANNEL_PC5 LL_ADC_CHANNEL_15