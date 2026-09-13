#pragma once

#include <cstdint>

enum HAL_StatusTypeDef
{
	HAL_OK,
	HAL_ERROR,
	HAL_BUSY,
	HAL_TIMEOUT
};

struct GPIO_TypeDef
{
};

struct TIM_TypeDef
{
	uint32_t CNT = 0;
	uint32_t CCR[4]{};
};

extern GPIO_TypeDef gpio_a;
extern TIM_TypeDef tim2, tim3, tim5;
#define GPIOA (&gpio_a)
#define TIM2 (&tim2)
#define TIM3 (&tim3)
#define TIM5 (&tim5)
#define GPIO_PIN_1 2
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_AF_PP 2
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_AF1_TIM2 1
#define TIM_CHANNEL_1 0
#define TIM_CHANNEL_2 4
#define TIM_CHANNEL_3 8
#define TIM_CHANNEL_4 12
#define TIM_OCPOLARITY_HIGH 0
#define TIM_OCPOLARITY_LOW 1
#define TIM_OCMODE_PWM1 6
#define TIM_OCFAST_DISABLE 0
#define IS_TIM_CCX_INSTANCE(t, c) ((t) == TIM2 || (t) == TIM3 || (t) == TIM5)
#define IS_TIM_OC_POLARITY(p) ((p) == 0 || (p) == 1)

struct TIM_HandleTypeDef
{
	TIM_TypeDef *Instance = nullptr;
};

struct TIM_OC_InitTypeDef
{
	uint32_t OCMode, Pulse, OCPolarity, OCFastMode;
};

#define __HAL_TIM_SET_COMPARE(h, c, v) ((h)->Instance->CCR[(c) / 4] = (v))
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *, TIM_OC_InitTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
