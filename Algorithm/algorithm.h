#ifndef __ALGORITHM_H
#define __ALGORITHM_H

#include <stdint.h>

#define ALGO_SAMPLE_RATE      25
#define ALGO_BUFFER_SIZE      100

typedef struct
{
	int32_t heart_rate;  
	int32_t spo2;        
	uint8_t hr_valid;    
	uint8_t spo2_valid;  
	uint8_t finger;      
} HR_SpO2_Result_t;

void Algorithm_Calc(const uint32_t *ir_buffer, const uint32_t *red_buffer,
                    int32_t length, HR_SpO2_Result_t *result);

#endif
