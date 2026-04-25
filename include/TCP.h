#pragma once
void TCP_init();
void TCP_maintainDHCP();
void TCP_processNetwork();
void TCP_syncFrom();
void TCP_syncTo();
void TCP_taskLoop(void *pvParameters);