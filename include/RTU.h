#pragma once

void RTU_init();
void RTU_process();
void RTU_syncFrom();
void RTU_syncTo();
void RTU_taskLoop(void *pvParameters);