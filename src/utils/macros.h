#pragma once

#define GLUE(a, b) a##b
#define GLUE2(x, y) GLUE(x, y)

#define GPIO(x) GLUE(GPIO, x)
#define GPIO_PIN(x) (1ul << x)
#define IO(port, pin) GLUE2(P, GLUE(port, pin))
#define GPIO_AF(x) GLUE(GpioPin::AfConfig::Af, x)

#define UART(x) GLUE(UART, x)
#define USART(x) GLUE(USART, x)
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


#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
